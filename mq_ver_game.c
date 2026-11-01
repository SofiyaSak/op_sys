#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define QMAXMSG  10

typedef struct { 
    int type; 
    int value; 
} msg_t;

enum { 
    MT_RANGE = 1, 
    MT_GUESS = 2, 
    MT_OK    = 3, 
    MT_UP    = 4, 
    MT_DOWN  = 5, 
    MT_STOP  = 6 
};

static void recv_msg(mqd_t q, msg_t *m) {
    if (mq_receive(q, (char *)m, sizeof *m, NULL) < 0) {
        perror("mq_receive");
        _exit(1);
    }
}

// ЛОГИКА УГАДЫВАЮЩЕГО
static void play_guesser(mqd_t q_in, mqd_t q_out, int round_num, const char* name) {
    msg_t m;
    recv_msg(q_in, &m); // получаем N
    if (m.type != MT_RANGE) _exit(1);
    
    int lo = 1, hi = m.value, attempts = 0;

    for (;;) {
        int g = lo + (hi - lo) / 2;
        ++attempts;
        
        printf("  %s Попытка %d: %d", name, attempts, g);
        fflush(stdout);

        msg_t out = { MT_GUESS, g };
        mq_send(q_out, (char *)&out, sizeof out, 0);

        recv_msg(q_in, &m);
        if (m.type == MT_OK) {
            printf(" -> УГАДАЛ!\n");
            printf("  [Статистика раунда %d] %s угадал за %d попыток.\n", 
                   round_num, name, attempts);
            fflush(stdout);
            
            // Отправляем подтверждение ведущему, что раунд окончен
            msg_t ack = { MT_STOP, 0 };
            mq_send(q_out, (char *)&ack, sizeof ack, 0);
            break;
        } else if (m.type == MT_UP) {
            printf(" -> больше\n");
            lo = g + 1;
        } else if (m.type == MT_DOWN) {
            printf(" -> меньше\n");
            hi = g - 1;
        } else {
            _exit(1);
        }
        fflush(stdout);
    }
}

// ЛОГИКА ЗАГАДЫВАЮЩЕГО
static void play_host(mqd_t q_in, mqd_t q_out, int N, int round_num, const char* name) {
    int secret = 1 + rand() % N;
    printf("  %s загадал число: %d\n", name, secret);
    fflush(stdout);
    
    msg_t m = { MT_RANGE, N };
    mq_send(q_out, (char *)&m, sizeof m, 0);

    for (;;) {
        recv_msg(q_in, &m); // получаем догадку
        if (m.type != MT_GUESS) _exit(1);
        int g = m.value;

        if (g == secret) {
            msg_t ok = { MT_OK, g };
            mq_send(q_out, (char *)&ok, sizeof ok, 0);
            
            recv_msg(q_in, &m);
            if (m.type != MT_STOP) _exit(1);
            break;
        } else {
            msg_t resp = { secret > g ? MT_UP : MT_DOWN, g };
            mq_send(q_out, (char *)&resp, sizeof resp, 0);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s N [rounds]\n", argv[0]); return 1; }
    int N = atoi(argv[1]);
    int rounds = (argc >= 3) ? atoi(argv[2]) : 10;
    if (N < 1 || rounds < 1) return 1;

    char qname_pc[64], qname_cp[64];
    snprintf(qname_pc, sizeof qname_pc, "/guess_pc_%d", (int)getpid());
    snprintf(qname_cp, sizeof qname_cp, "/guess_cp_%d", (int)getpid());

    struct mq_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg = QMAXMSG;
    attr.mq_msgsize = sizeof(msg_t); 

    //Родитель создает очереди
    mqd_t q_pc = mq_open(qname_pc, O_CREAT | O_RDWR | O_EXCL, 0600, &attr);
    mqd_t q_cp = mq_open(qname_cp, O_CREAT | O_RDWR | O_EXCL, 0600, &attr);
    if (q_pc == (mqd_t)-1 || q_cp == (mqd_t)-1) { perror("mq_open parent"); return 1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        // РЕБЕНОК - ИГРОК 2
        srand((unsigned)time(NULL) ^ getpid());
        
        mqd_t c_pc = mq_open(qname_pc, O_RDWR);
        mqd_t c_cp = mq_open(qname_cp, O_RDWR);
        if (c_pc == (mqd_t)-1 || c_cp == (mqd_t)-1) { perror("mq_open child"); _exit(1); }
        mq_close(q_pc);
        mq_close(q_cp);

        for (int r = 0; r < rounds; ++r) {
            printf("\n--- РАУНД %d ---\n", r + 1);
            if (r % 2 == 0) {
                play_guesser(c_pc, c_cp, r + 1, "[Игрок 2]");
            } else {
                play_host(c_cp, c_pc, N, r + 1, "[Игрок 2]");
            }
        }
        
        mq_close(c_pc); 
        mq_close(c_cp);
        fflush(stdout);
        exit(0);
    } else {
        // РОДИТЕЛЬ - ИГРОК 1
        srand((unsigned)time(NULL) ^ getpid());
        for (int r = 0; r < rounds; ++r) {
            if (r % 2 == 0) {
                play_host(q_cp, q_pc, N, r + 1, "[Игрок 1]");
            } else {
                play_guesser(q_pc, q_cp, r + 1, "[Игрок 1]");
            }
        }
        
        int st; 
        waitpid(pid, &st, 0);
        
        mq_close(q_pc); 
        mq_close(q_cp);
        mq_unlink(qname_pc); 
        mq_unlink(qname_cp);
        
        printf("\nИгра завершена.\n");
        fflush(stdout);
    }
    return 0;
}
