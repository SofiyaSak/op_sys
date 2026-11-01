#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

static volatile sig_atomic_t flag_number = 0;  // N или догадка
static volatile sig_atomic_t val_number  = 0;  // Само число
static volatile sig_atomic_t flag_result = 0;  // Ответ от ведущего
static volatile sig_atomic_t is_guessed  = 0;  // 1 - угадал, 0 - не угадал
static volatile sig_atomic_t hint        = 0;  // 1 - больше, 2 - меньше

// Обработчик для приема чисел через сигналы реального времени
static void h_number(int sig, siginfo_t *si, void *ctx) {
    (void)sig; (void)ctx;
    val_number = si->si_value.sival_int;
    flag_number = 1;
}

// Обработчик ответа "Угадал"
static void h_ok(int sig) {
    (void)sig;
    is_guessed = 1;
    flag_result = 1;
}

// Обработчик ответа "Не угадал" (с подсказкой)
static void h_fail(int sig, siginfo_t *si, void *ctx) {
    (void)sig; (void)ctx;
    is_guessed = 0;
    hint = si->si_value.sival_int; 
    flag_result = 1;
}

// Функция атомарного ожидания сигнала
static void wait_flag(volatile sig_atomic_t *flag) {
    sigset_t empty;
    sigemptyset(&empty);
    while (!(*flag)) {
        sigsuspend(&empty);
    }
}

// Настройка обработчиков
static void install_handlers(void) {
    struct sigaction sa;

    // Для приема чисел (SIGRTMIN)
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = h_number;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, NULL);

    // Для ответа "Угадал" (SIGUSR1)
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = h_ok;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    // Для ответа "Не угадал" (SIGUSR2)
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = h_fail;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
}


// ЛОГИКА УГАДЫВАЮЩЕГО
static void play_guesser(pid_t opponent, int round_num, const char* name) {
    wait_flag(&flag_number); 
    
    int lo = 1, hi = (int)val_number, attempts = 0;

    for (;;) {
        int g = lo + (hi - lo) / 2;
        ++attempts;
        
        printf("  %s Попытка %d: %d", name, attempts, g);
        fflush(stdout);
        flag_result = 0;
        
        // Отправляем догадку
        union sigval sv;
        sv.sival_int = g;
        sigqueue(opponent, SIGRTMIN, sv);

        // Ждем ответа
        wait_flag(&flag_result);

        if (is_guessed) {
            printf(" -> УГАДАЛ!\n");
            printf("  [Статистика раунда %d] %s угадал за %d попыток.\n", 
                   round_num, name, attempts);
            fflush(stdout);
            break;
        } else {
            if (hint == 1) {
                printf(" -> больше\n");
                lo = g + 1;
            } else {
                printf(" -> меньше\n");
                hi = g - 1;
            }
            fflush(stdout);
        }
    }
}

// ЛОГИКА ЗАГАДЫВАЮЩЕГО
static void play_host(pid_t opponent, int N, int round_num, const char* name) {
    int secret = 1 + rand() % N;
    printf("  %s загадал число: %d\n", name, secret);
    fflush(stdout);
    flag_number = 0;
    // Отправляем границу N
    union sigval sv;
    sv.sival_int = N;
    sigqueue(opponent, SIGRTMIN, sv);

    for (;;) {
        // Ждем догадку
        wait_flag(&flag_number);
        int g = (int)val_number;
        flag_number = 0;
        if (g == secret) {
            // Отправляем "Угадал"
            union sigval empty_sv; 
            empty_sv.sival_int = 0;
            sigqueue(opponent, SIGUSR1, empty_sv);
            break;
        } else {
            // Отправляем "Не угадал" + подсказку
            union sigval hint_sv;
            hint_sv.sival_int = (secret > g) ? 1 : 2;
            sigqueue(opponent, SIGUSR2, hint_sv);
        }
    }
}

int main(int argc, char **argv) // Подаем N и число раундов при желании (по умолчанию их 10)
{
    if (argc < 2) { fprintf(stderr, "usage: %s N [rounds]\n", argv[0]); return 1; }
    int N = atoi(argv[1]);
    int rounds = (argc >= 3) ? atoi(argv[2]) : 10;
    if (N < 1 || rounds < 1) return 1;
    install_handlers();

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        // РЕБЕНОК - ИГРОК 2
        install_handlers();
        srand((unsigned)time(NULL) ^ getpid());

        for (int r = 0; r < rounds; ++r) {
            printf("\n--- РАУНД %d ---\n", r + 1);
            if (r % 2 == 0) {
                play_guesser(getppid(), r + 1, "[Игрок 2]");
            } else {
                play_host(getppid(), N, r + 1, "[Игрок 2]");
            }
        }
        
        fflush(stdout);
        exit(0);
    } else {
        // РОДИТЕЛЬ - ИГРОК 1
        srand((unsigned)time(NULL) ^ getpid());

        for (int r = 0; r < rounds; ++r) {
            if (r % 2 == 0) {
                play_host(pid, N, r + 1, "[Игрок 1]");
            } else {
                play_guesser(pid, r + 1, "[Игрок 1]");
            }
        }
        
        int st; 
        waitpid(pid, &st, 0);
        
        printf("\nИгра завершена.\n");
        fflush(stdout);
    }
    return 0;
}