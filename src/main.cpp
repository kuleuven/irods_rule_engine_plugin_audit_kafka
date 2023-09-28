// irods includes
#include "irods/private/audit_kafka.hpp"
#include "irods/private/audit_b64enc.hpp"
#include <irods/irods_logger.hpp>
#include <irods/irods_re_plugin.hpp>
#include <irods/irods_re_serialization.hpp>
#include <irods/irods_server_properties.hpp>

// boost includes
#include <boost/any.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/config.hpp>
#include <boost/exception/all.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>

// misc includes
#include <nlohmann/json.hpp>
#include <fmt/core.h>
#include <fmt/compile.h>

// stl includes
#include <cstdint>
#include <version>
#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <chrono>
#include <map>
#include <mutex>
#include <regex>

// kafka includes
#include <librdkafka/rdkafka.h>

namespace irods::plugin::rule_engine::audit_kafka
{
	namespace
	{
		const auto pep_regex_flavor = std::regex::ECMAScript;

		// NOLINTBEGIN(cert-err58-cpp, cppcoreguidelines-avoid-non-const-global-variables)
		const std::string_view default_pep_regex_to_match{"audit_.*"};
		const std::string_view default_kafka_brokers{"localhost:9092"};
		const std::string_view default_kafka_topic{"irods_audit_messages"};

		std::string audit_pep_regex_to_match;
		std::string audit_kafka_brokers;
		std::string audit_kafka_topic;

		// audit_pep_regex is initially populated with an unoptimized default, as optimization
		// makes construction slower, and we don't expect it to be used before configuration is read.
		std::regex audit_pep_regex{audit_pep_regex_to_match, pep_regex_flavor};

        rd_kafka_t *rk;        /* Producer instance handle */

		std::mutex audit_plugin_mutex;
		// NOLINTEND(cert-err58-cpp, cppcoreguidelines-avoid-non-const-global-variables)
	} // namespace

	static BOOST_FORCEINLINE void set_default_configs()
	{
		audit_pep_regex_to_match = default_pep_regex_to_match;
		audit_kafka_brokers = default_kafka_brokers;
		audit_kafka_topic = default_kafka_topic;

		audit_pep_regex = std::regex(audit_pep_regex_to_match, pep_regex_flavor | std::regex::optimize);
	}

	static auto get_re_configs(const std::string& _instance_name) -> irods::error
	{
		try {
			const auto& rule_engines = irods::get_server_property<const nlohmann::json&>(
				std::vector<std::string>{irods::KW_CFG_PLUGIN_CONFIGURATION, irods::KW_CFG_PLUGIN_TYPE_RULE_ENGINE});
			for (const auto& rule_engine : rule_engines) {
				const auto& inst_name = rule_engine.at(irods::KW_CFG_INSTANCE_NAME).get_ref<const std::string&>();
				if (inst_name != _instance_name) {
					continue;
				}

				if (rule_engine.count(irods::KW_CFG_PLUGIN_SPECIFIC_CONFIGURATION) <= 0) {
					set_default_configs();
					// clang-format off
					log_re::debug({
						{"rule_engine_plugin", rule_engine_name},
						{"log_message", "Using default plugin configuration"},
						{"instance_name", _instance_name},
					});
					// clang-format on

					return SUCCESS();
				}

				const auto& plugin_spec_cfg = rule_engine.at(irods::KW_CFG_PLUGIN_SPECIFIC_CONFIGURATION);

				audit_pep_regex_to_match = plugin_spec_cfg.at("pep_regex_to_match").get<std::string>();

				audit_kafka_topic = plugin_spec_cfg.at("kafka_topic").get_ref<const std::string&>();
				audit_kafka_brokers = plugin_spec_cfg.at("kafka_brokers").get_ref<const std::string&>();

				audit_pep_regex = std::regex(audit_pep_regex_to_match, pep_regex_flavor | std::regex::optimize);

				return SUCCESS();
			}
		}
		catch (const std::out_of_range& e) {
			return ERROR(KEY_NOT_FOUND, e.what());
		}
		catch (const nlohmann::json::exception& e) {
			return ERROR(SYS_LIBRARY_ERROR, e.what());
		}
		catch (const std::exception& e) {
			return ERROR(SYS_INTERNAL_ERR, e.what());
		}
		catch (...) {
			return ERROR(SYS_UNKNOWN_ERROR, "an unknown error occurred");
		}

		return ERROR(SYS_INVALID_INPUT_PARAM, "failed to find plugin configuration");
	}


	/**
	 * @brief Message delivery report callback.
	 *
	 * This callback is called exactly once per message, indicating if
	 * the message was succesfully delivered
	 * (rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR) or permanently
	 * failed delivery (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR).
	 *
	 * The callback is triggered from rd_kafka_poll() and executes on
	 * the application's thread.
	 */
	static void dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque) {
		if (rkmessage->err) {
			// clang-format off
			log_re::error({
				{"rule_engine_plugin", rule_engine_name},
				{"log_message", "Message delivery failed"},
				{"error_result", rd_kafka_err2str(rkmessage->err)},
			});
			// clang-format on
		}

		/* The rkmessage is destroyed automatically by librdkafka */
	}

	static auto start([[maybe_unused]] irods::default_re_ctx& _re_ctx, const std::string& _instance_name)
		-> irods::error
	{
		std::lock_guard<std::mutex> lock(audit_plugin_mutex);

		irods::error ret = get_re_configs(_instance_name);
		if (!ret.ok()) {
			// clang-format off
			log_re::error({
				{"rule_engine_plugin", rule_engine_name},
				{"log_message", "Error loading plugin configuration"},
				{"instance_name", _instance_name},
				{"error_result", ret.result()},
			});
			// clang-format on
		}

        rd_kafka_conf_t *conf; /* Temporary configuration object */
        char errstr[512];      /* librdkafka API error reporting buffer */

        conf = rd_kafka_conf_new();

        if (rd_kafka_conf_set(conf, "bootstrap.servers", audit_kafka_brokers.c_str(), errstr,
                    sizeof(errstr)) != RD_KAFKA_CONF_OK) {
			return ERROR(SYS_INTERNAL_ERR, errstr);
        }

		rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

        rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
        if (!rk) {
			return ERROR(SYS_INTERNAL_ERR, errstr);
        }
		
		return SUCCESS();
	}

	static auto stop([[maybe_unused]] irods::default_re_ctx& _re_ctx, const std::string& _instance_name) -> irods::error
	{
		std::lock_guard<std::mutex> lock(audit_plugin_mutex);

        rd_kafka_flush(rk, 10 * 1000 /* wait for max 10 seconds */);

        /* If the output queue is still not empty there is an issue
         * with producing messages to the clusters. */
        if (rd_kafka_outq_len(rk) > 0) {
			// clang-format off
            log_re::error({
						{"rule_engine_plugin", rule_engine_name},
						{"log_message", "failed to flush messages kafka topic"},
					});
			// clang-format on
        }

        /* Destroy the producer instance */
        rd_kafka_destroy(rk);

        return SUCCESS();
	}

	static auto rule_exists([[maybe_unused]] irods::default_re_ctx& _re_ctx, const std::string& _rn, bool& _ret)
		-> irods::error
	{
		try {
			std::smatch matches;
			_ret = std::regex_match(_rn, matches, audit_pep_regex);
		}
		catch (const std::exception& _e) {
			return ERROR(SYS_INTERNAL_ERR, _e.what());
		}
		catch (...) {
			return ERROR(SYS_UNKNOWN_ERROR, "an unknown error occurred");
		}

		return SUCCESS();
	}

	static auto list_rules(
		[[maybe_unused]] irods::default_re_ctx& _re_ctx,
		[[maybe_unused]] std::vector<std::string>& _rules) -> irods::error
	{
		return SUCCESS();
	}

	static auto exec_rule(
		[[maybe_unused]] irods::default_re_ctx& _re_ctx,
		const std::string& _rn,
		std::list<boost::any>& _ps,
		irods::callback _eff_hdlr) -> irods::error
	{
		std::lock_guard<std::mutex> lock(audit_plugin_mutex);

		// stores a counter of unique arg types
		std::map<std::string, std::size_t> arg_type_map;

		ruleExecInfo_t* rei = nullptr;
		irods::error err = _eff_hdlr("unsafe_ms_ctx", &rei);
		if (!err.ok()) {
			return err;
		}

		nlohmann::json json_obj;

		std::string msg_str;

		try {
			std::uint64_t time_ms = ts_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
			json_obj["@timestamp"] = time_ms;
			json_obj["hostname"] = boost::asio::ip::host_name();
			json_obj["pid"] = getpid();
			json_obj["rule_name"] = _rn;

			for (const auto& itr : _ps) {
				// The BytesBuf parameter should not be serialized because this commonly contains
				// the entirety of the contents of files. These could be very big and cause the
				// message broker to explode.
				if (std::type_index(typeid(BytesBuf*)) == std::type_index(itr.type())) {
					// clang-format off
					log_re::trace({
						{"rule_engine_plugin", rule_engine_name},
						{"log_message", "skipping serialization of BytesBuf parameter"},
						{"rule_name", _rn},
					});
					// clang-format on
					continue;
				}

				// serialize the parameter to a map
				irods::re_serialization::serialized_parameter_t param;
				irods::error ret = irods::re_serialization::serialize_parameter(itr, param);
				if (!ret.ok()) {
					// clang-format off
					log_re::error({
						{"rule_engine_plugin", rule_engine_name},
						{"log_message", "failed to serialize argument"},
						{"rule_name", _rn},
						{"error_result", ret.result()},
					});
					// clang-format on
					continue;
				}

				for (const auto& elem : param) {
					const std::string& arg = elem.first;

					std::size_t ctr;
					const auto iter = arg_type_map.find(arg);
					if (iter == arg_type_map.end()) {
						arg_type_map.insert(std::make_pair(arg, static_cast<std::size_t>(1)));
						ctr = 1;
					}
					else {
						ctr = iter->second + 1;
						iter->second = ctr;
					}

					if (ctr > 1) {
						const std::string key = fmt::format(FMT_COMPILE("{0:s}__{1:d}"), arg, ctr);
						insert_as_string_or_base64(json_obj, key, elem.second, time_ms);
					}
					else {
						insert_as_string_or_base64(json_obj, arg, elem.second, time_ms);
					}
				}
			}

			msg_str = json_obj.dump();
            char * c = msg_str.data();

            size_t len = strlen(c);
            rd_kafka_resp_err_t err;

            retry:
            err = rd_kafka_producev(
                    /* Producer handle */
                    rk,
                    /* Topic name */
                    RD_KAFKA_V_TOPIC(audit_kafka_topic.c_str()),
                    /* Make a copy of the payload. */
                    RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                    /* Message value and length */
                    RD_KAFKA_V_VALUE(c, len),
                    /* Per-Message opaque, provided in
                     *                      * delivery report callback as
                     *                                           * msg_opaque. */
                    RD_KAFKA_V_OPAQUE(NULL),
                    /* End sentinel */
                    RD_KAFKA_V_END);
        
            if (err) {
                log_re::error({
						{"rule_engine_plugin", rule_engine_name},
						{"log_message", "failed to produce to kafka topic"},
						{"rule_name", _rn},
                        {"kafka_topic", audit_kafka_topic},
						{"kafka_error", rd_kafka_err2str(err)},
					});

                if (err == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
                    rd_kafka_poll(rk,
                            1000 /*block for max 1000ms*/);
                    goto retry;
                }
            }

            rd_kafka_poll(rk, 0 /*non-blocking*/);
		}
		catch (const irods::exception& e) {
			log_exception(e, "Caught iRODS exception", {"rule_name", _rn});
			return ERROR(e.code(), e.what());
		}
		catch (const nlohmann::json::exception& e) {
			log_exception(e, "Caught nlohmann-json exception", {"rule_name", _rn});
			return ERROR(SYS_LIBRARY_ERROR, e.what());
		}
		catch (const std::exception& e) {
			log_exception(e, "Caught exception", {"rule_name", _rn});
			return ERROR(SYS_INTERNAL_ERR, e.what());
		}
		catch (...) {
			return ERROR(SYS_UNKNOWN_ERROR, "an unknown error occurred");
		}

		return CODE(RULE_ENGINE_CONTINUE);
	}
} // namespace irods::plugin::rule_engine::audit_kafka

//
// Plugin Factory
//

using pluggable_rule_engine = irods::pluggable_rule_engine<irods::default_re_ctx>;

extern "C" auto plugin_factory(const std::string& _inst_name, const std::string& _context) -> pluggable_rule_engine*
{
	using namespace irods::plugin::rule_engine::audit_kafka;

	set_default_configs();

	const auto not_supported = [](auto&&...) { return ERROR(SYS_NOT_SUPPORTED, "Not supported."); };

	auto* rule_engine = new irods::pluggable_rule_engine<irods::default_re_ctx>(_inst_name, _context);

	rule_engine->add_operation("start", std::function<irods::error(irods::default_re_ctx&, const std::string&)>(start));

	rule_engine->add_operation("stop", std::function<irods::error(irods::default_re_ctx&, const std::string&)>(stop));

	rule_engine->add_operation(
		"rule_exists", std::function<irods::error(irods::default_re_ctx&, const std::string&, bool&)>(rule_exists));

	rule_engine->add_operation(
		"list_rules", std::function<irods::error(irods::default_re_ctx&, std::vector<std::string>&)>(list_rules));

	rule_engine->add_operation(
		"exec_rule",
		std::function<irods::error(
			irods::default_re_ctx&, const std::string&, std::list<boost::any>&, irods::callback)>(exec_rule));

	rule_engine->add_operation(
		"exec_rule_text",
		std::function<irods::error(
			irods::default_re_ctx&, const std::string&, msParamArray_t*, const std::string&, irods::callback)>(
			not_supported));

	rule_engine->add_operation(
		"exec_rule_expression",
		std::function<irods::error(irods::default_re_ctx&, const std::string&, msParamArray_t*, irods::callback)>(
			not_supported));

	return rule_engine;
}
