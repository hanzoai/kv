
case "$1" in
    start)
        if [ -f $PIDFILE ]
        then
            echo "$PIDFILE exists, process is already running or crashed"
        else
            echo "Starting KV server..."
            $EXEC $CONF
        fi
        ;;
    stop)
        if [ ! -f $PIDFILE ]
        then
            echo "$PIDFILE does not exist, process is not running"
        else
            PID=$(cat $PIDFILE)
            echo "Stopping ..."
            $CLIEXEC -p $KVPORT shutdown
            while [ -x /proc/${PID} ]
            do
                echo "Waiting for KV to shutdown ..."
                sleep 1
            done
            echo "KV stopped"
        fi
        ;;
    status)
        PID=$(cat $PIDFILE)
        if [ ! -x /proc/${PID} ]
        then
            echo 'KV is not running'
        else
            echo "KV is running ($PID)"
        fi
        ;;
    restart)
        $0 stop
        $0 start
        ;;
    *)
        echo "Please use start, stop, restart or status as first argument"
        ;;
esac
