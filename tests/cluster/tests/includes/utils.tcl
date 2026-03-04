source "../../../tests/support/cli.tcl"

proc config_set_all_nodes {keyword value} {
    foreach_kv_id id {
        R $id config set $keyword $value
    }
}

proc fix_cluster {addr} {
    set code [catch {
        exec $::KV_CLI_BIN {*}[kvcli_tls_config "../../../tests"] --cluster fix $addr << yes
    } result]
    if {$code != 0} {
        puts "kv-cli --cluster fix returns non-zero exit code, output below:\n$result"
    }
    # Note: kv-cli --cluster fix may return a non-zero exit code if nodes don't agree,
    # but we can ignore that and rely on the check below.
    assert_cluster_state ok
    wait_for_condition 100 100 {
        [catch {exec $::KV_CLI_BIN {*}[kvcli_tls_config "../../../tests"] --cluster check $addr} result] == 0
    } else {
        puts "kv-cli --cluster check returns non-zero exit code, output below:\n$result"
        fail "Cluster could not settle with configuration"
    }
}

proc wait_cluster_stable {} {
    wait_for_condition 1000 50 {
        [catch {exec $::KV_CLI_BIN --cluster \
            check 127.0.0.1:[get_instance_attrib kv 0 port] \
            {*}[kvcli_tls_config "../../../tests"] \
            }] == 0
    } else {
        fail "Cluster doesn't stabilize"
    }
}