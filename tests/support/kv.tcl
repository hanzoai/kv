# Tcl client library - used by the server test
# Copyright (c) 2009-2014 Redis Ltd.
# Released under the BSD license like Redis itself
#
# Example usage:
#
# set r [kv 127.0.0.1 6379]
# $r lpush mylist foo
# $r lpush mylist bar
# $r lrange mylist 0 -1
# $r close
#
# Non blocking usage example:
#
# proc handlePong {r type reply} {
#     puts "PONG $type '$reply'"
#     if {$reply ne "PONG"} {
#         $r ping [list handlePong]
#     }
# }
#
# set r [kv]
# $r blocking 0
# $r get fo [list handlePong]
#
# vwait forever

package provide kv 0.1

source [file join [file dirname [info script]] "response_transformers.tcl"]

namespace eval kv {}
set ::kv::id 0
array set ::kv::fd {}
array set ::kv::addr {}
array set ::kv::blocking {}
array set ::kv::deferred {}
array set ::kv::readraw {}
array set ::kv::attributes {} ;# Holds the RESP3 attributes from the last call
array set ::kv::reconnect {}
array set ::kv::tls {}
array set ::kv::callback {}
array set ::kv::state {} ;# State in non-blocking reply reading
array set ::kv::statestack {} ;# Stack of states, for nested mbulks
array set ::kv::curr_argv {} ;# Remember the current argv, to be used in response_transformers.tcl
array set ::kv::testing_resp3 {} ;# Indicating if the current client is using RESP3 (only if the test is trying to test RESP3 specific behavior. It won't be on in case of force_resp3)

set ::force_resp3 0
set ::log_req_res 0

proc kv {{server 127.0.0.1} {port 6379} {defer 0} {tls 0} {tlsoptions {}} {readraw 0}} {
    if {$tls} {
        package require tls
        ::tls::init \
            -cafile "$::tlsdir/ca.crt" \
            -certfile "$::tlsdir/client.crt" \
            -keyfile "$::tlsdir/client.key" \
            {*}$tlsoptions
        set fd [::tls::socket $server $port]
    } else {
        set fd [socket $server $port]
    }
    fconfigure $fd -translation binary
    set id [incr ::kv::id]
    set ::kv::fd($id) $fd
    set ::kv::addr($id) [list $server $port]
    set ::kv::blocking($id) 1
    set ::kv::deferred($id) $defer
    set ::kv::readraw($id) $readraw
    set ::kv::reconnect($id) 0
    set ::kv::curr_argv($id) 0
    set ::kv::testing_resp3($id) 0
    set ::kv::tls($id) $tls
    ::kv::kv_reset_state $id
    interp alias {} ::kv::kvHandle$id {} ::kv::__dispatch__ $id
}

# On recent versions of tcl-tls/OpenSSL, reading from a dropped connection
# results with an error we need to catch and mimic the old behavior.
proc ::kv::kv_safe_read {fd len} {
    if {$len == -1} {
        set err [catch {set val [read $fd]} msg]
    } else {
        set err [catch {set val [read $fd $len]} msg]
    }
    if {!$err} {
        return $val
    }
    if {[string match "*connection abort*" $msg]} {
        return {}
    }
    error $msg
}

proc ::kv::kv_safe_gets {fd} {
    if {[catch {set val [gets $fd]} msg]} {
        if {[string match "*connection abort*" $msg]} {
            return {}
        }
        error $msg
    }
    return $val
}

# This is a wrapper to the actual dispatching procedure that handles
# reconnection if needed.
proc ::kv::__dispatch__ {id method args} {
    set errorcode [catch {::kv::__dispatch__raw__ $id $method $args} retval]
    if {$errorcode && $::kv::reconnect($id) && $::kv::fd($id) eq {}} {
        # Try again if the connection was lost.
        # FIXME: we don't re-select the previously selected DB, nor we check
        # if we are inside a transaction that needs to be re-issued from
        # scratch.
        set errorcode [catch {::kv::__dispatch__raw__ $id $method $args} retval]
    }
    return -code $errorcode $retval
}

proc ::kv::__dispatch__raw__ {id method argv} {
    set fd $::kv::fd($id)

    # Reconnect the link if needed.
    if {$fd eq {} && $method ne {close}} {
        lassign $::kv::addr($id) host port
        if {$::kv::tls($id)} {
            set ::kv::fd($id) [::tls::socket $host $port]
        } else {
            set ::kv::fd($id) [socket $host $port]
        }
        fconfigure $::kv::fd($id) -translation binary
        set fd $::kv::fd($id)
    }

    # Transform HELLO 2 to HELLO 3 if force_resp3
    # All set the connection var testing_resp3 in case of HELLO 3
    if {[llength $argv] > 0 && [string compare -nocase $method "HELLO"] == 0} {
        if {[lindex $argv 0] == 3} {
            set ::kv::testing_resp3($id) 1
        } else {
            set ::kv::testing_resp3($id) 0
            if {$::force_resp3} {
                # If we are in force_resp3 we run HELLO 3 instead of HELLO 2
                lset argv 0 3
            }
        }
    }

    set blocking $::kv::blocking($id)
    set deferred $::kv::deferred($id)
    if {$blocking == 0} {
        if {[llength $argv] == 0} {
            error "Please provide a callback in non-blocking mode"
        }
        set callback [lindex $argv end]
        set argv [lrange $argv 0 end-1]
    }
    if {[info command ::kv::__method__$method] eq {}} {
        catch {unset ::kv::attributes($id)}
        set cmd "*[expr {[llength $argv]+1}]\r\n"
        append cmd "$[string length $method]\r\n$method\r\n"
        foreach a $argv {
            append cmd "$[string length $a]\r\n$a\r\n"
        }
        ::kv::kv_write $fd $cmd
        if {[catch {flush $fd}]} {
            catch {close $fd}
            set ::kv::fd($id) {}
            return -code error "I/O error reading reply"
        }

        set ::kv::curr_argv($id) [concat $method $argv]
        if {!$deferred} {
            if {$blocking} {
                ::kv::kv_read_reply $id $fd
            } else {
                # Every well formed reply read will pop an element from this
                # list and use it as a callback. So pipelining is supported
                # in non blocking mode.
                lappend ::kv::callback($id) $callback
                fileevent $fd readable [list ::kv::kv_readable $fd $id]
            }
        }
    } else {
        uplevel 1 [list ::kv::__method__$method $id $fd] $argv
    }
}

proc ::kv::__method__blocking {id fd val} {
    set ::kv::blocking($id) $val
    fconfigure $fd -blocking $val
}

proc ::kv::__method__reconnect {id fd val} {
    set ::kv::reconnect($id) $val
}

proc ::kv::__method__read {id fd} {
    ::kv::kv_read_reply $id $fd
}

proc ::kv::__method__rawread {id fd {len -1}} {
    return [kv_safe_read $fd $len]
}

proc ::kv::__method__write {id fd buf} {
    ::kv::kv_write $fd $buf
}

proc ::kv::__method__flush {id fd} {
    flush $fd
}

proc ::kv::__method__close {id fd} {
    catch {close $fd}
    catch {unset ::kv::fd($id)}
    catch {unset ::kv::addr($id)}
    catch {unset ::kv::blocking($id)}
    catch {unset ::kv::deferred($id)}
    catch {unset ::kv::readraw($id)}
    catch {unset ::kv::attributes($id)}
    catch {unset ::kv::reconnect($id)}
    catch {unset ::kv::tls($id)}
    catch {unset ::kv::state($id)}
    catch {unset ::kv::statestack($id)}
    catch {unset ::kv::callback($id)}
    catch {unset ::kv::curr_argv($id)}
    catch {unset ::kv::testing_resp3($id)}
    catch {interp alias {} ::kv::kvHandle$id {}}
}

proc ::kv::__method__channel {id fd} {
    return $fd
}

proc ::kv::__method__deferred {id fd val} {
    set ::kv::deferred($id) $val
}

proc ::kv::__method__readraw {id fd val} {
    set ::kv::readraw($id) $val
}

proc ::kv::__method__readingraw {id fd} {
    return $::kv::readraw($id)
}

proc ::kv::__method__attributes {id fd} {
    set _ $::kv::attributes($id)
}

proc ::kv::kv_write {fd buf} {
    puts -nonewline $fd $buf
}

proc ::kv::kv_writenl {fd buf} {
    kv_write $fd $buf
    kv_write $fd "\r\n"
    flush $fd
}

proc ::kv::kv_readnl {fd len} {
    set buf [kv_safe_read $fd $len]
    kv_safe_read $fd 2 ; # discard CR LF
    return $buf
}

proc ::kv::kv_bulk_read {fd} {
    set count [kv_read_line $fd]
    if {$count == -1} return {}
    set buf [kv_readnl $fd $count]
    return $buf
}

proc ::kv::redis_multi_bulk_read {id fd} {
    set count [kv_read_line $fd]
    if {$count == -1} return {}
    set l {}
    set err {}
    for {set i 0} {$i < $count} {incr i} {
        if {[catch {
            lappend l [kv_read_reply_logic $id $fd]
        } e] && $err eq {}} {
            set err $e
        }
    }
    if {$err ne {}} {return -code error $err}
    return $l
}

proc ::kv::kv_read_map {id fd} {
    set count [kv_read_line $fd]
    if {$count == -1} return {}
    set d {}
    set err {}
    for {set i 0} {$i < $count} {incr i} {
        if {[catch {
            set k [kv_read_reply_logic $id $fd] ; # key
            set v [kv_read_reply_logic $id $fd] ; # value
            dict set d $k $v
        } e] && $err eq {}} {
            set err $e
        }
    }
    if {$err ne {}} {return -code error $err}
    return $d
}

proc ::kv::kv_read_line fd {
    string trim [kv_safe_gets $fd]
}

proc ::kv::kv_read_null fd {
    kv_safe_gets $fd
    return {}
}

proc ::kv::kv_read_bool fd {
    set v [kv_read_line $fd]
    if {$v == "t"} {return 1}
    if {$v == "f"} {return 0}
    return -code error "Bad protocol, '$v' as bool type"
}

proc ::kv::kv_read_double {id fd} {
    set v [kv_read_line $fd]
    # unlike many other DTs, there is a textual difference between double and a string with the same value,
    # so we need to transform to double if we are testing RESP3 (i.e. some tests check that a
    # double reply is "1.0" and not "1")
    if {[should_transform_to_resp2 $id]} {
        return $v
    } else {
        return [expr {double($v)}]
    }
}

proc ::kv::kv_read_verbatim_str fd {
    set v [kv_bulk_read $fd]
    # strip the first 4 chars ("txt:")
    return [string range $v 4 end]
}

proc ::kv::kv_read_reply_logic {id fd} {
    if {$::kv::readraw($id)} {
        return [kv_read_line $fd]
    }

    while {1} {
        set type [kv_safe_read $fd 1]
        switch -exact -- $type {
            _ {return [kv_read_null $fd]}
            : -
            ( -
            + {return [kv_read_line $fd]}
            , {return [kv_read_double $id $fd]}
            # {return [kv_read_bool $fd]}
            = {return [kv_read_verbatim_str $fd]}
            - {return -code error [kv_read_line $fd]}
            $ {return [kv_bulk_read $fd]}
            > -
            ~ -
            * {return [redis_multi_bulk_read $id $fd]}
            % {return [kv_read_map $id $fd]}
            | {
                set attrib [kv_read_map $id $fd]
                set ::kv::attributes($id) $attrib
                continue
            }
            default {
                if {$type eq {}} {
                    catch {close $fd}
                    set ::kv::fd($id) {}
                    return -code error "I/O error reading reply"
                }
                return -code error "Bad protocol, '$type' as reply type byte"
            }
        }
    }
}

proc ::kv::kv_read_reply {id fd} {
    set response [kv_read_reply_logic $id $fd]
    ::response_transformers::transform_response_if_needed $id $::kv::curr_argv($id) $response
}

proc ::kv::kv_reset_state id {
    set ::kv::state($id) [dict create buf {} mbulk -1 bulk -1 reply {}]
    set ::kv::statestack($id) {}
}

proc ::kv::kv_call_callback {id type reply} {
    set cb [lindex $::kv::callback($id) 0]
    set ::kv::callback($id) [lrange $::kv::callback($id) 1 end]
    uplevel #0 $cb [list ::kv::kvHandle$id $type $reply]
    ::kv::kv_reset_state $id
}

# Read a reply in non-blocking mode.
proc ::kv::kv_readable {fd id} {
    if {[eof $fd]} {
        kv_call_callback $id eof {}
        ::kv::__method__close $id $fd
        return
    }
    if {[dict get $::kv::state($id) bulk] == -1} {
        set line [gets $fd]
        if {$line eq {}} return ;# No complete line available, return
        switch -exact -- [string index $line 0] {
            : -
            + {kv_call_callback $id reply [string range $line 1 end-1]}
            - {kv_call_callback $id err [string range $line 1 end-1]}
            ( {kv_call_callback $id reply [string range $line 1 end-1]}
            $ {
                dict set ::kv::state($id) bulk \
                    [expr [string range $line 1 end-1]+2]
                if {[dict get $::kv::state($id) bulk] == 1} {
                    # We got a $-1, hack the state to play well with this.
                    dict set ::kv::state($id) bulk 2
                    dict set ::kv::state($id) buf "\r\n"
                    ::kv::kv_readable $fd $id
                }
            }
            * {
                dict set ::kv::state($id) mbulk [string range $line 1 end-1]
                # Handle *-1
                if {[dict get $::kv::state($id) mbulk] == -1} {
                    kv_call_callback $id reply {}
                }
            }
            default {
                kv_call_callback $id err \
                    "Bad protocol, $type as reply type byte"
            }
        }
    } else {
        set totlen [dict get $::kv::state($id) bulk]
        set buflen [string length [dict get $::kv::state($id) buf]]
        set toread [expr {$totlen-$buflen}]
        set data [read $fd $toread]
        set nread [string length $data]
        dict append ::kv::state($id) buf $data
        # Check if we read a complete bulk reply
        if {[string length [dict get $::kv::state($id) buf]] ==
            [dict get $::kv::state($id) bulk]} {
            if {[dict get $::kv::state($id) mbulk] == -1} {
                kv_call_callback $id reply \
                    [string range [dict get $::kv::state($id) buf] 0 end-2]
            } else {
                dict with ::kv::state($id) {
                    lappend reply [string range $buf 0 end-2]
                    incr mbulk -1
                    set bulk -1
                }
                if {[dict get $::kv::state($id) mbulk] == 0} {
                    kv_call_callback $id reply \
                        [dict get $::kv::state($id) reply]
                }
            }
        }
    }
}

# when forcing resp3 some tests that rely on resp2 can fail, so we have to translate the resp3 response to resp2
proc ::kv::should_transform_to_resp2 {id} {
    return [expr {$::force_resp3 && !$::kv::testing_resp3($id)}]
}
