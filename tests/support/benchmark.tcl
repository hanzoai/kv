proc kvbenchmark_tls_config {testsdir} {
    set tlsdir [file join $testsdir tls]
    set cert [file join $tlsdir client.crt]
    set key [file join $tlsdir client.key]
    set cacert [file join $tlsdir ca.crt]

    if {$::tls} {
        return [list --tls --cert $cert --key $key --cacert $cacert]
    } else {
        return {}
    }
}

proc kvbenchmark {host port {opts {}}} {
    set cmd [list $::KV_BENCHMARK_BIN -h $host -p $port]
    lappend cmd {*}[kvbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

proc kvbenchmarkuri {host port {opts {}}} {
    set cmd [list $::KV_BENCHMARK_BIN -u kv://$host:$port]
    lappend cmd {*}[kvbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

proc kvbenchmarkuriuserpass {host port user pass {opts {}}} {
    set cmd [list $::KV_BENCHMARK_BIN -u kv://$user:$pass@$host:$port]
    lappend cmd {*}[kvbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}
