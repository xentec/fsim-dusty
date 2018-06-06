#include "sds011.hpp"

using namespace asio;
using namespace std::placeholders;

#define SYNC_BYTE 0xAA

namespace {
enum CMD : u8
{
	ReportMode = 2,
	Query = 4,
	DeviceID = 5,
	WorkState = 6,
	Firmware = 7,
	Cycle = 8,

	Sample = 0xC0,
	Reply = 0xC5,
};

enum MODE : u8
{
	Get = 0,
	Set = 1
};
#pragma pack(push,1)
struct Request
{
	u8 hdr; // 0xAA
	u8 send; // 0xB4
	CMD cmd;
	MODE mode;
	u8 data[11];
	u16 devID;
	u8 cksum;
	u8 tail; // 0xAB
};

struct Response
{
	u8 hdr; // 0xAA
	u8 cmd;
	u8 data[6];
	u8 cksum;
	u8 tail; // 0xAB
};
#pragma pack(pop)

enum class State
{
	Sleep, WarmUp, Measure
};

template<class Iter>
static auto chksum(const Range<Iter>& r) -> u8
{
	u8 sum = 0;
	for(const u8& e : r)
		sum += e;

	return sum;
}

template<class Iter>
static bool valid(const Range<Iter>& r)
{
	auto p = range(std::next(r.begin()), std::next(r.begin(), 7));
	return chksum(p) == *p.end();
}
}

SDS011::SDS011(io_context& ioctx, const std::string& dev_path, slog::sink_ptr sink)
	: logger(slog::create("sds011", sink))
	, dev(ioctx, dev_path)
	, timeout(ioctx)
	, parse_state{SYNC}
{
	using opt = serial_port_base;
	dev.set_option(opt::baud_rate(9600));
	dev.set_option(opt::flow_control(opt::flow_control::none));
	tcdrain(dev.native_handle());

	recv_start();

	get_firmware([this](auto ec, Version ver)
	{
		if(ec)
		{
			logger->warn("failed to check version: {}", ec.message());
			return;
		}
		logger->debug("firmware: {}-{}-{}", ver.year, ver.month, ver.day);
	});
}

SDS011::~SDS011()
{}

void SDS011::send_cmd(u8 cmd, ResponseCB cb) { send_cmd(cmd, Get, false, 0, cb); }
void SDS011::send_cmd(u8 cmd, u8 mode, u8 data, ResponseCB cb) { send_cmd(cmd, mode, true, data, cb); }
void SDS011::send_cmd(u8 cmd, u8 mode, bool hasMode, u8 data, ResponseCB cb)
{
	static u32 id = 0;

	bool first = q_w.empty();
	logger->debug("sending cmd: {:02X} - {} {}", cmd, mode == Get ? "Get" : "Set", mode == Get ? "" : fmt::FormatInt(data).c_str());
	q_w.push_back({++id, cmd, mode, data, hasMode, cb ?: [](...){}});
	if(first)
		send_start();
}

void SDS011::send_start()
{
	if(q_w.empty())
		return;

	auto &r = q_w.front();
	std::ostream w(&buf_w);

	w | arr<u8>(SYNC_BYTE, 0xB4, r.cmd, r.mode, r.data);

	std::fill_n(std::ostream_iterator<u8>(w), 10, 0);

	w | arr<u8>(0xFF, 0xFF);
	auto data = buf_w.data_range();
	w | chksum(range(std::next(data.begin(), 2), data.end()));
	w | u8(0xAB);

	data = buf_w.data_range();
	logger->trace("SEND: {:02X}", fmt::join(data.begin()+1, data.end()-1, " "));

	dev.async_write_some(buf_w.data(), std::bind(&SDS011::send_handler, this, _1, _2));
}

void SDS011::send_handler(error_code ec, usz len)
{
	buf_w.consume(len);

	if(q_w.empty()) return;

	const auto r = std::move(q_w.front());
	q_w.pop_front();

	if(ec) { r.cb(ec, {}); return; }
	q_r.push_back(std::move(r));
	timeout.expires_from_now(std::chrono::milliseconds(500));
	timeout.async_wait([this](error_code ec)
	{
		if(ec.value() == error::operation_aborted) return;

		q_w.push_back(q_r.front());
		q_r.pop_front();
		send_start();
	});
}


void SDS011::recv_start()
{
	dev.async_read_some(buf_r.prepare(64), std::bind(&SDS011::recv_handler, this, _1, _2));
}

void SDS011::recv_handler(error_code ec, usz len)
{
	if(ec)
	{
		switch(ec.value())
		{
		case error::operation_aborted: break; // TODO: check state
		default:
			logger->warn("failed to recv data: {}", ec.message());
			throw std::system_error(ec);
		}
		return;
	}

	constexpr usz pktSize = sizeof(Response)-2;
	buf_r.commit(len);

	bool stop = false;
	do {
		auto pkt(buf_r.data_range());

		switch(parse_state)
		{
		case SYNC:
			pkt.begin() = std::find(pkt.begin(), pkt.end(), SYNC_BYTE);
			if(pkt.empty())
			{
				stop = true; break;
			}

			pkt.advance(); // drop sync byte
			buf_r.consume(1);
			parse_state = DATA;
		case DATA:
			if(pkt.size() < pktSize+1)
			{
				stop = true; break;
			}
			if(auto tail = std::next(pkt.begin(), pktSize); *tail == 0xAB) // [SYNC [pkt] SYNC+pktSize]
			{
				pkt.end() = std::next(tail);
				if(!valid(pkt))
				{
					logger->debug("RECV: crc fail: {:02X}", fmt::join(pkt.begin(), pkt.end(), " "));
				}
				else
				{
					logger->trace("RECV: {:02X}", fmt::join(pkt.begin(), pkt.end()-2, " "));
					recv_packet(pkt);
					buf_r.consume(pkt.size());
				}
			}
		default:
			parse_state = SYNC;
		}
	} while(!stop);

	recv_start();
}

void SDS011::recv_packet(const Buffer::const_range& data)
{
	static auto conv = [](auto &p)
	{
		u16 v = *std::next(p) << 8 | *p;
		std::advance(p, 2);
		return f32(v)/10.f;
	};

	auto itr = data.begin();
	const u8 type = *itr++;
	switch(type)
	{
	case CMD::Sample:
	{
		f32 pm2_5 = conv(itr), pm10 = conv(itr);
		logger->debug("sample: {:.1f}, {:.1f}", pm2_5, pm10);
		if(on_samples) on_samples(pm2_5, pm10);

		if(!q_r.empty() && q_r.front().cmd == Query)
		{
			timeout.cancel();
			logger->debug("confirmed: {:02X}", Query);
			q_r.pop_front();
			send_start();
		}

		break;
	}
	case CMD::Reply:
		timeout.cancel();
		logger->debug("recving res: {:02X} - {} {:02X}", itr[0], itr[1] == Get ? "Get" : "Set", itr[2]);
		while(!q_r.empty())
		{
			Request &rq = q_r.front();
			bool found = rq.cmd == itr[0];
			if(rq.has_mode) found &= rq.mode == itr[1];
			if(found)
			{
				logger->debug("confirmed: {:02X}", rq.cmd);
				rq.cb({}, range(itr, data.end()));
			}
			q_r.pop_front();

			if (found) break; // stop only after completing one request, all previous become invalid
		}
		send_start();
		break;
	default:
		logger->debug("case not handled: {:02x}", type);
	}
}


void SDS011::report_mode(ReportModeCB cb)
{
	change_report_mode(Get, 0, cb);
}

void SDS011::set_report_mode(bool passive, ReportModeCB cb)
{
	change_report_mode(Set, passive, cb);
}

void SDS011::change_report_mode(u8 mode, bool passive, ReportModeCB cb)
{
	ResponseCB rcb {};
	if(cb)
		rcb = [cb](auto ec, Buffer::const_range buf)
		{
			if(ec)
				cb(ec, {});
			else
				cb({}, !!buf[2]);
		};

	send_cmd(Cycle, mode, passive, rcb);
}


void SDS011::state(StateCB cb)
{
	change_state(Get, 0, cb);
}

void SDS011::set_state(bool active, StateCB cb)
{
	logger->debug(active ? "starting" : "stopping");
	change_state(Set, active, cb);
}

void SDS011::change_state(u8 mode, bool active, StateCB cb)
{
	ResponseCB rcb {};
	if(cb)
		rcb = [cb](auto ec, Buffer::const_range buf)
		{
			if(ec)
				cb(ec, {});
			else
				cb({}, !!buf[2]);
		};

	send_cmd(WorkState, mode, active, rcb);
}


void SDS011::cycle(CycleCB cb)
{
	change_cycle(Get, 0, cb);
}

void SDS011::set_cycle(u8 min, CycleCB cb)
{
	logger->debug("setting cycle mode to {} min", min);
	change_cycle(Set, min, cb);
}

void SDS011::change_cycle(u8 mode, u8 new_cycle, CycleCB cb)
{
	ResponseCB rcb {};
	if(cb)
		rcb = [cb](auto ec, Buffer::const_range buf)
		{
			if(ec)
				cb(ec, {});
			else
				cb({}, buf[2]);
		};

	send_cmd(Cycle, mode, new_cycle, rcb);
}


void SDS011::get_firmware(FirmwareCB cb)
{
	if(!cb) return;
	send_cmd(Firmware, [cb](auto ec, Buffer::const_range i)
	{
		if(ec) { cb(ec, {}); return; }
		cb({}, Version{i[1], i[2], i[3]});
	});
}

void SDS011::poll()
{
	send_cmd(Query, {});
}
