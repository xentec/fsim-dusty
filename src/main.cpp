
#include "global.hpp"
#include "logger.hpp"
#include "sds011.hpp"

#include <argh.h>
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>

#include <spdlog/sinks/dist_sink.h>

struct Conf
{
	u32 interval_sample = 5;
	bool stdout_color;
	struct
	{
		std::string log_dir;
	} paths;
} conf;


int main(int argc, char* argv[])
{
	argh::parser opts(argc, argv);

	if(opts[{"-V", "--version"}])
	{
		fmt::print("dusty v0.1\n");
		return 0;
	}

	auto log_lvl = slog::level::info;
	if (opts["-v"]) log_lvl = slog::level::debug;
	if (opts["-vv"]) log_lvl = slog::level::trace;
	slog::set_level(log_lvl);

	conf.stdout_color = opts["--color"];
	opts({"-n", "--interval"}, conf.interval_sample) >> conf.interval_sample;
	opts({"-L", "--log-dir"}) >> conf.paths.log_dir;
	if(!conf.paths.log_dir.empty())
		conf.paths.log_dir += '/';

	slog::set_level(log_lvl);
	slog::set_pattern("[%Y-%m-%d %H:%M:%S %L] %n: %v");

	auto sink = std::make_shared<slog::sinks::dist_sink_st>();

	// log with colors only in tty mode (e.g. when run manually)
	if(conf.stdout_color || isatty(STDOUT_FILENO))
		sink->add_sink(std::make_shared<slog::sinks::ansicolor_stdout_sink_st>());
	else
		sink->add_sink(std::make_shared<slog::sinks::stdout_sink_st>());


	auto logger = slog::create("main", sink);
	logger->info("dusty v0.1");

	auto json_log = slog::rotating_logger_st("json", conf.paths.log_dir + "dusty.json.log", 1024*1024*5, 5);
	json_log->set_pattern("%v");
	json_log->flush_on(slog::level::info);

	asio::io_context ioctx;
	SDS011 sensor(ioctx, SDS011_PATH, sink);
	sensor.on_samples = [&](f32 pm2_5_cur, f32 pm10_cur)
	{
		static f32 pm2_5_pre = 0.f, pm10_pre = 0.f;

		if(pm2_5_pre == pm2_5_cur && pm10_pre == pm10_cur)
			return;

		pm2_5_pre = pm2_5_cur, pm10_pre = pm10_cur;

		json_log->info("{{ \"ts\": {}, \"PM2.5\": {:.01f}, \"PM10\": {:.01f} }},",
					   time(nullptr), pm2_5_cur, pm10_cur);
	};
	sensor.get_firmware([=](auto ec, SDS011::Version v)
	{
		if(ec) return;
		logger->info("SDS011 version: {}-{}-{}", v.year, v.month, v.day);
	});
	sensor.state([&](auto ec, bool active)
	{
		if(ec) return;
		if(active)
			sensor.poll();
	});
	sensor.set_state(true);
	sensor.set_cycle(0);
	sensor.set_cycle(conf.interval_sample, [&](auto ec, auto iv)
	{
		if(ec) return;
		logger->info("sampling interval: {} min", iv);
	});

	asio::signal_set signals(ioctx, SIGTERM, SIGINT);
	signals.async_wait([&](auto, int){ ioctx.stop(); });


	ioctx.run();
	return 0;
}
