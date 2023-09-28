from __future__ import print_function

from irods.configuration import IrodsConfig

def main():
    irods_config = IrodsConfig()
    irods_config.server_config['plugin_configuration']['rule_engines'].insert(1,
            {
                "instance_name": "irods_rule_engine_plugin-audit_kafka-instance",
                "plugin_name": "irods_rule_engine_plugin-audit_kafka",
                "plugin_specific_configuration" : {
                     "kafka_brokers" : "localhost:9092",
                     "kafka_topic" : "audit_messages",
                     "pep_regex_to_match" : "audit_.*",
                     "test_mode": "true"
                 }
            }
        )
    irods_config.server_config["rule_engine_namespaces"].append("audit_")
    irods_config.commit(irods_config.server_config, irods_config.server_config_path, make_backup=True)

if __name__ == '__main__':
    main()
