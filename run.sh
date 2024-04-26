#!/bin/sh

if [ $# -le 0 ]; then
    echo "Run: $0 -h for usage"
    exit 1
fi
while [ $# -gt 0 ]; do
    set -e
    case "$1" in
        -h)
            set +e
            echo "Usage: $0"
            echo "  -h: help"
            echo "  -mr: make release"
            echo "  -md: make debug"
            echo "  -rp <rideable #> <test #> <thread #>: run perf on ./bin/main -r <rideable #> -m <test #> -t <thread #>"
            echo "  -rt <rideable #> <test #> <thread #>: run ./bin/main -r <rideable #> -m <test #> -t <1 to thread #>"
            exit 0
            ;;
        -mr)
            make clean
            make
            shift
            ;;
        -md)
            make clean
            make BUILD=debug
            shift
            ;;
        -rp)
            set +e
            rm -rf /mnt/pmem/"$USER"*
            perf stat -e rtm_retired.aborted,rtm_retired.aborted_events,rtm_retired.aborted_mem,rtm_retired.aborted_memtype,rtm_retired.aborted_timer,rtm_retired.aborted_unfriendly,rtm_retired.commit,rtm_retired.start ./bin/main -r "$2" -m "$3" -t "$4"
            shift 4
            ;;
        -rt)
            set +e
            threads=$(seq 1 "$4")
            it_num=$(seq 1 "$5")
            for _ in $it_num; do
                for i in $threads; do
                    rm -rf /mnt/pmem/"$USER"*
                    ./bin/main -r "$2" -m "$3" -t "$i"
                done
            done
            shift 5
            ;;
        *)
            echo "Run: $0 -h for usage"
            exit 1
            ;;
    esac
done
