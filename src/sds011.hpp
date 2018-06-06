#pragma once

#include "global.hpp"
#include "type/ptr.hpp"

#include "buffer.hpp"
#include "logger.hpp"

#include <asio/serial_port.hpp>
#include <asio/steady_timer.hpp>

#include <deque>

#define SDS011_PATH "/dev/ttyUSB0"

struct SDS011
{
	SDS011(asio::io_context& ioctx, const std::string& dev_path = SDS011_PATH, slog::sink_ptr sink = std::make_shared<slog::sinks::null_sink_st>());
	SDS011(const SDS011&) = delete;
	SDS011(SDS011&&) = delete;
	virtual ~SDS011();

	using ReportModeCB = std::function<void(asio::error_code, bool)>;
	void report_mode(ReportModeCB cb);
	void set_report_mode(bool passive, ReportModeCB cb = {});

	using StateCB = std::function<void(asio::error_code, bool)>;
	void state(StateCB cb);
	void set_state(bool active, StateCB cb = {});

	using CycleCB = std::function<void(asio::error_code, u8)>;
	void cycle(CycleCB cb);
	void set_cycle(u8 min, CycleCB cb = {});

	struct Version { u8 year, month, day; };
	using FirmwareCB = std::function<void(asio::error_code, Version)>;
	void get_firmware(FirmwareCB cb);

	void poll();
	std::function<void(f32 pm2_5, f32 pm10)> on_samples;
private:
	using ResponseCB = std::function<void(asio::error_code ec, Buffer::const_range)>;

	void operator()(asio::error_code ec, std::size_t length);

	void recv_start();
	void recv_handler(asio::error_code ec, usz len);
	void recv_packet(const Buffer::const_range& data);

	void send_start();
	void send_handler(asio::error_code ec, usz len);

	void send_cmd(u8 cmd, u8 mode, bool hasMode, u8 data, ResponseCB cb);
	void send_cmd(u8 cmd, ResponseCB cb);
	void send_cmd(u8 cmd, u8 mode, u8 data, ResponseCB cb);

	void change_report_mode(u8 mode, bool passive, ReportModeCB cb);
	void change_state(u8 mode, bool active, StateCB cb);
	void change_cycle(u8 mode, u8 new_cycle, CycleCB cb);

	ptrS<slog::logger> logger;
	asio::serial_port dev;
	asio::steady_timer timeout;

	enum ParseState { SYNC, DATA } parse_state;

	struct Request
	{
		u32 id;
		u8 cmd, mode, data;
		bool has_mode;
		ResponseCB cb;
	};
	std::deque<Request> q_r, q_w;
	Buffer buf_r, buf_w;
};
