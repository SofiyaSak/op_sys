#!/usr/bin/env bash
set -u

PROG_NAME="$(basename "$0")"
LOG_PATH=""
ERR_PATH=""

show_help() {
    cat <<EOF
Использование: $PROG_NAME [ОПЦИИ]

Опции:
  -u, --users        Вывести список пользователей и их домашних
                     директорий, отсортированный по алфавиту.
  -p, --processes    Вывести список запущенных процессов,
                     отсортированный по идентификатору (PID).
  -l, --log PATH     Перенаправить стандартный вывод в файл PATH.
  -e, --errors PATH  Перенаправить поток ошибок (stderr) в файл PATH.
  -h, --help         Показать справку.

Примеры:
  $PROG_NAME --users
  $PROG_NAME -p -l /home/vboxuser/processes.txt
  $PROG_NAME -u -e /home/vboxuser/errors.log
EOF
}

check_path() {
    local path="$1"
    local dir
    dir="$(dirname "$path")"

    if [[ ! -d "$dir" ]]; then
        echo "Ошибка: каталог '$dir' не существует." >&2
        return 1
    fi
    if [[ ! -w "$dir" ]]; then
        echo "Ошибка: нет прав на запись в каталог '$dir'." >&2
        return 1
    fi
    # Если файл существует — проверяем право на запись в него
    if [[ -e "$path" && ! -w "$path" ]]; then
        echo "Ошибка: нет прав на запись в файл '$path'." >&2
        return 1
    fi
    return 0
}

list_users() {
    awk -F: '{ print $1 "\t" $6 }' /etc/passwd | sort
}

list_processes() {
    ps -eo pid,user,comm --no-headers 2>/dev/null | sort -n -k1,1
}
parse_args() {
    local parsed
    if ! parsed=$(getopt -o uphl:e: \
                    --long users,processes,help,log:,errors: \
                    -n "$PROG_NAME" -- "$@"); then
        echo "Ошибка: неверные аргументы командной строки." >&2
        echo "Запустите '$PROG_NAME --help' для справки." >&2
        exit 1
    fi

    eval set -- "$parsed"

    ACTION=""
    while true; do
        case "$1" in
            -u|--users)
                ACTION="users"
                shift
                ;;
            -p|--processes)
                ACTION="processes"
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            -l|--log)
                LOG_PATH="$2"
                shift 2
                ;;
            -e|--errors)
                ERR_PATH="$2"
                shift 2
                ;;
            --)
                shift
                break
                ;;
            *)
                echo "Внутренняя ошибка разбора аргументов: '$1'" >&2
                exit 1
                ;;
        esac
    done

    if [[ -n "$LOG_PATH" ]] && ! check_path "$LOG_PATH"; then
        exit 1
    fi
    if [[ -n "$ERR_PATH" ]] && ! check_path "$ERR_PATH"; then
        exit 1
    fi
}

main() {
    parse_args "$@"
    if [[ -n "$ERR_PATH" ]]; then
        exec 2>>"$ERR_PATH"
    fi

    case "$ACTION" in
        users)
            if [[ -n "$LOG_PATH" ]]; then
                list_users >>"$LOG_PATH"
            else
                list_users
            fi
            ;;
        processes)
            if [[ -n "$LOG_PATH" ]]; then
                list_processes >>"$LOG_PATH"
            else
                list_processes
            fi
            ;;
        "")
            echo "Ошибка: не указано действие." >&2
            show_help >&2
            exit 1
            ;;
    esac
}

main "$@"
