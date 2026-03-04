proc kvcli_tls_config {testsdir} {
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

# Returns command line for executing kv-cli
proc kvcli {host port {opts {}}} {
    set cmd [list $::KV_CLI_BIN -h $host -p $port]
    lappend cmd {*}[kvcli_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

proc kvcliuri {scheme host port {opts {}}} {
    set cmd [list $::KV_CLI_BIN -u $scheme$host:$port]
    lappend cmd {*}[kvcli_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

# Returns command line for executing kv-cli with a unix socket address
proc kvcli_unixsocket {unixsocket {opts {}}} {
    return [list $::KV_CLI_BIN -s $unixsocket {*}$opts]
}

# Run kv-cli with specified args on the server of specified level.
# Returns output broken down into individual lines.
proc kvcli_exec {level args} {
    set cmd [kvcli_unixsocket [srv $level unixsocket] $args]
    set fd [open "|$cmd" "r"]
    set ret [lrange [split [read $fd] "\n"] 0 end-1]
    close $fd

    return $ret
}
