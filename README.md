# iRODS Rule Engine Plugin - Audit via Kafka

This C++ plugin provides the iRODS platform a rule engine that can emit a single kafka message to the configured topic for every policy enforcement point (PEP) encountered by the iRODS server.

# Build

Building the iRODS Audit Rule Engine Plugin requires iRODS 4.3.0+ (http://github.com/irods/irods).

This plugin requires the iRODS development and runtime packages to be installed on the build machine.

Also, use the iRODS-built CMake (or CMake 3.11+):

```
export PATH=/opt/irods-externals/cmake3.21.4-0/bin:$PATH
```

```
cd irods_rule_engine_plugin_audit_kafka
mkdir build
cd build
cmake ../
make package
```

# Install

The packages produced by CMake will install the Audit plugin shared object file:

`/usr/lib/irods/plugins/rule_engines/libirods_rule_engine_plugin-audit_kafka.so`

# Configuration

After installing the plugin, `/etc/irods/server_config.json` needs to be configured to use the plugin.

Add a new stanza to the "rule_engines" array within `server_config.json`:

```json
            {
                "instance_name": "irods_rule_engine_plugin-audit_kafka-instance",
                "plugin_name": "irods_rule_engine_plugin-audit_kafka",
                "plugin_specific_configuration" : {
                     "kafka_brokers" : "localhost:9092",
                     "kafka_topic" : "audit_messages",
                     "pep_regex_to_match" : "audit_.*"
                 }
            },
```

Add the new `audit_` namespace to the "rule_engine_namespaces" array within `server_config.json`:

```
    "rule_engine_namespaces": [
        "", 
        "audit_"
    ], 
```
