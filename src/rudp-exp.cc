#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("RudpExp");

// ---------------------------------------------------------------- packet format
// type(1) transferId(4) seq(4) len(2) checksum(2) = 13 bytes, then payload
static const uint8_t T_DATA = 1;
static const uint8_t T_ACK = 2;
static const uint32_t HDR = 13;

static uint16_t
Checksum(const std::vector<uint8_t>& b)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < b.size(); i += 2)
    {
        uint32_t w = static_cast<uint32_t>(b[i]) << 8;
        if (i + 1 < b.size())
        {
            w |= b[i + 1];
        }
        sum += w;
    }
    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum & 0xffff);
}

static Ptr<Packet>
Build(uint8_t type, uint32_t id, uint32_t seq, const uint8_t* p, uint16_t len)
{
    std::vector<uint8_t> b(HDR + len, 0);
    b[0] = type;
    for (int i = 0; i < 4; i++)
    {
        b[1 + i] = (id >> (24 - 8 * i)) & 0xff;
        b[5 + i] = (seq >> (24 - 8 * i)) & 0xff;
    }
    b[9] = len >> 8;
    b[10] = len & 0xff;
    if (len)
    {
        std::copy(p, p + len, b.begin() + HDR);
    }
    uint16_t c = Checksum(b);
    b[11] = c >> 8;
    b[12] = c & 0xff;
    return Create<Packet>(b.data(), b.size());
}

struct Parsed
{
    uint8_t type = 0;
    uint32_t id = 0;
    uint32_t seq = 0;
    uint16_t len = 0;
    std::vector<uint8_t> payload;
};

static uint32_t
Rd32(const std::vector<uint8_t>& b, int o)
{
    return (static_cast<uint32_t>(b[o]) << 24) | (static_cast<uint32_t>(b[o + 1]) << 16) |
           (static_cast<uint32_t>(b[o + 2]) << 8) | b[o + 3];
}

// returns false if packet is malformed or checksum fails
static bool
Parse(Ptr<Packet> pkt, double corruptProb, Ptr<UniformRandomVariable> rv, Parsed& out)
{
    std::vector<uint8_t> b(pkt->GetSize());
    pkt->CopyData(b.data(), b.size());
    if (b.size() < HDR)
    {
        return false;
    }
    if (corruptProb > 0 && rv->GetValue() < corruptProb)
    {
        b[rv->GetInteger(0, b.size() - 1)] ^= 0x5a; // simulated bit corruption
    }
    uint16_t rc = (static_cast<uint16_t>(b[11]) << 8) | b[12];
    b[11] = b[12] = 0;
    if (Checksum(b) != rc)
    {
        return false;
    }
    out.type = b[0];
    out.id = Rd32(b, 1);
    out.seq = Rd32(b, 5);
    out.len = (static_cast<uint16_t>(b[9]) << 8) | b[10];
    if (out.len != b.size() - HDR)
    {
        return false;
    }
    out.payload.assign(b.begin() + HDR, b.end());
    return true;
}

static uint64_t
Fnv(const std::vector<uint8_t>& v)
{
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t c : v)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// ---------------------------------------------------------------- globals
static uint32_t g_fileSize = 1000000;
static uint32_t g_payload = 1000;
static uint32_t g_N = 0;
static const uint32_t g_id = 7;

// ---------------------------------------------------------------- receiver
class Receiver
{
  public:
    Receiver(Ptr<Node> n, uint16_t port, double corrupt)
        : m_node(n),
          m_port(port),
          m_corrupt(corrupt)
    {
        m_buf.assign(g_fileSize, 0);
        m_rv = CreateObject<UniformRandomVariable>();
    }

    void Start()
    {
        m_sock = Socket::CreateSocket(m_node, UdpSocketFactory::GetTypeId());
        m_sock->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_port));
        m_sock->SetRecvCallback(MakeCallback(&Receiver::OnRecv, this));
    }

    void OnRecv(Ptr<Socket> s)
    {
        Ptr<Packet> p;
        Address from;
        while ((p = s->RecvFrom(from)))
        {
            Parsed r;
            if (!Parse(p, m_corrupt, m_rv, r))
            {
                badPkts++;
                continue;
            }
            if (r.type != T_DATA || r.id != g_id)
            {
                continue;
            }
            if (r.seq == m_expected)
            {
                uint32_t off = r.seq * g_payload;
                std::copy(r.payload.begin(), r.payload.end(), m_buf.begin() + off);
                m_expected++;
                if (m_expected == g_N)
                {
                    complete = true;
                }
            }
            else if (r.seq < m_expected)
            {
                dups++;
            }
            else
            {
                outOfOrder++;
            }
            s->SendTo(Build(T_ACK, g_id, m_expected, nullptr, 0), 0, from); // cumulative ACK
        }
    }

    std::vector<uint8_t> m_buf;
    bool complete = false;
    uint32_t badPkts = 0;
    uint32_t dups = 0;
    uint32_t outOfOrder = 0;

  private:
    Ptr<Node> m_node;
    uint16_t m_port;
    double m_corrupt;
    Ptr<Socket> m_sock;
    Ptr<UniformRandomVariable> m_rv;
    uint32_t m_expected = 0;
};

// ---------------------------------------------------------------- sender (Go-Back-N)
class Sender
{
  public:
    Sender(Ptr<Node> n,
           Ipv4Address peer,
           uint16_t port,
           const std::vector<uint8_t>& file,
           bool adaptive,
           uint32_t fixedWin,
           double fixedRto,
           uint32_t maxWin,
           double corrupt,
           std::ofstream* trace)
        : m_node(n),
          m_peer(peer),
          m_port(port),
          m_file(file),
          m_adaptive(adaptive),
          m_fixedWin(fixedWin),
          m_maxWin(maxWin),
          m_corrupt(corrupt),
          m_trace(trace)
    {
        m_rv = CreateObject<UniformRandomVariable>();
        m_sentAt.assign(g_N, Time(0));
        m_everSent.assign(g_N, 0);
        m_retxFlag.assign(g_N, 0);
        m_rto = adaptive ? 0.3 : fixedRto; // adaptive starts with a conservative guess
        m_cwnd = 4.0;
        m_ssthresh = maxWin;
    }

    void Start()
    {
        m_sock = Socket::CreateSocket(m_node, UdpSocketFactory::GetTypeId());
        m_sock->Bind();
        m_sock->Connect(InetSocketAddress(m_peer, m_port));
        m_sock->SetRecvCallback(MakeCallback(&Sender::OnRecv, this));
        m_start = Simulator::Now();
        TrySend();
    }

    bool done = false;
    Time doneTime;
    Time m_start;
    uint64_t dataSent = 0;
    uint64_t retx = 0;
    uint64_t timeouts = 0;

  private:
    void Trace()
    {
        if (m_trace)
        {
            *m_trace << Simulator::Now().GetSeconds() << "," << m_cwnd << "," << m_rto << ","
                     << m_srtt << "," << m_base << "\n";
        }
    }

    void SendData(uint32_t seq)
    {
        uint32_t off = seq * g_payload;
        uint16_t len = std::min<uint32_t>(g_payload, g_fileSize - off);
        m_sock->Send(Build(T_DATA, g_id, seq, &m_file[off], len));
        dataSent++;
        if (m_everSent[seq])
        {
            retx++;
            m_retxFlag[seq] = 1; // Karn: never sample RTT from this packet
        }
        else
        {
            m_everSent[seq] = 1;
        }
        m_sentAt[seq] = Simulator::Now();
    }

    void TrySend()
    {
        uint32_t w = m_adaptive
                         ? std::min<uint32_t>(std::max<uint32_t>(1, (uint32_t)m_cwnd), m_maxWin)
                         : m_fixedWin;
        while (m_next < g_N && m_next - m_base < w)
        {
            SendData(m_next);
            m_next++;
        }
        if (!m_timer.IsPending() && m_base < m_next)
        {
            m_timer = Simulator::Schedule(Seconds(m_rto), &Sender::OnTimeout, this);
        }
    }

    void OnTimeout()
    {
        m_timer = EventId();
        if (done)
        {
            return;
        }
        timeouts++;
        if (m_adaptive)
        {
            m_ssthresh = std::max(m_cwnd / 2.0, 2.0);
            m_cwnd = 1.0;
            m_rto = std::min(m_rto * 2.0, m_maxRto); // exponential backoff
        }
        m_dup = 0;
        m_next = m_base; // Go-Back-N: resend whole window
        Trace();
        TrySend();
    }

    void UpdateRtt(double r)
    {
        if (!m_haveRtt)
        {
            m_srtt = r;
            m_rttvar = r / 2.0;
            m_haveRtt = true;
        }
        else
        {
            m_rttvar = 0.75 * m_rttvar + 0.25 * std::fabs(m_srtt - r);
            m_srtt = 0.875 * m_srtt + 0.125 * r;
        }
        double rto = m_srtt + std::max(0.001, 4.0 * m_rttvar);
        m_rto = std::min(std::max(rto, m_minRto), m_maxRto);
    }

    void OnRecv(Ptr<Socket> s)
    {
        Ptr<Packet> p;
        Address from;
        while ((p = s->RecvFrom(from)))
        {
            Parsed r;
            if (!Parse(p, m_corrupt, m_rv, r) || r.type != T_ACK || r.id != g_id)
            {
                continue;
            }
            OnAck(r.seq);
        }
    }

    void OnAck(uint32_t ack)
    {
        if (done || ack > g_N)
        {
            return;
        }
        if (ack > m_base)
        {
            uint32_t newly = ack - m_base;
            if (!m_retxFlag[ack - 1] && m_adaptive)
            {
                UpdateRtt((Simulator::Now() - m_sentAt[ack - 1]).GetSeconds());
            }
            m_base = ack;
            m_dup = 0;
            if (m_adaptive)
            {
                for (uint32_t i = 0; i < newly; i++)
                {
                    m_cwnd += (m_cwnd < m_ssthresh) ? 1.0 : 1.0 / m_cwnd; // slow start / AIMD
                }
                m_cwnd = std::min<double>(m_cwnd, m_maxWin);
            }
            m_timer.Cancel();
            m_timer = EventId();
            if (m_next < m_base)
            {
                m_next = m_base;
            }
            Trace();
            if (m_base >= g_N)
            {
                done = true;
                doneTime = Simulator::Now();
                Simulator::Stop();
                return;
            }
            TrySend();
        }
        else if (ack == m_base && m_adaptive)
        {
            m_dup++;
            if (m_dup == 3) // fast retransmit (once per loss event)
            {
                m_ssthresh = std::max(m_cwnd / 2.0, 2.0);
                m_cwnd = m_ssthresh;
                m_dup = -1000000;
                m_timer.Cancel();
                m_timer = EventId();
                m_next = m_base;
                Trace();
                TrySend();
            }
        }
    }

    Ptr<Node> m_node;
    Ipv4Address m_peer;
    uint16_t m_port;
    const std::vector<uint8_t>& m_file;
    bool m_adaptive;
    uint32_t m_fixedWin;
    uint32_t m_maxWin;
    double m_corrupt;
    std::ofstream* m_trace;
    Ptr<Socket> m_sock;
    Ptr<UniformRandomVariable> m_rv;
    EventId m_timer;

    uint32_t m_base = 0;
    uint32_t m_next = 0;
    int m_dup = 0;
    double m_cwnd;
    double m_ssthresh;
    double m_rto;
    double m_srtt = 0;
    double m_rttvar = 0;
    bool m_haveRtt = false;
    double m_minRto = 0.02;
    double m_maxRto = 2.0;
    std::vector<Time> m_sentAt;
    std::vector<uint8_t> m_everSent;
    std::vector<uint8_t> m_retxFlag;
};

// ---------------------------------------------------------------- TCP trace
static uint64_t g_tcpBytes = 0;
static bool g_tcpDone = false;
static double g_tcpTime = 0;
static void
TcpRx(Ptr<const Packet> p, const Address&)
{
    g_tcpBytes += p->GetSize();
    if (!g_tcpDone && g_tcpBytes >= g_fileSize)
    {
        g_tcpDone = true;
        g_tcpTime = Simulator::Now().GetSeconds();
        Simulator::Stop();
    }
}

static void
PrintResult(const std::string& mode,
            double loss,
            double delay,
            double rate,
            uint32_t run,
            double t,
            uint64_t sent,
            long retx,
            long to,
            int ok)
{
    double gp = (t > 0) ? (g_fileSize * 8.0 / t / 1e6) : 0;
    std::cout << "RESULT," << mode << "," << loss << "," << delay << "," << rate << "," << run
              << "," << std::fixed << std::setprecision(4) << t << "," << gp << "," << sent << ","
              << retx << "," << to << "," << ok << std::endl;
}

// ---------------------------------------------------------------- main
int
main(int argc, char* argv[])
{
    std::string mode = "adaptive"; // tcp | fixed | adaptive
    double loss = 0.0;
    double delayMs = 10.0;
    double rateMbps = 5.0;
    double fixedRto = 0.2;
    double corrupt = 0.0;
    uint32_t run = 1;
    uint32_t fixedWin = 16;
    uint32_t maxWin = 128;
    bool trace = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("mode", "tcp | fixed | adaptive", mode);
    cmd.AddValue("loss", "per-packet loss probability on both directions", loss);
    cmd.AddValue("delay", "one-way link delay in ms", delayMs);
    cmd.AddValue("rate", "link rate in Mbps", rateMbps);
    cmd.AddValue("fileSize", "file size in bytes", g_fileSize);
    cmd.AddValue("payload", "payload bytes per packet", g_payload);
    cmd.AddValue("run", "RNG run number", run);
    cmd.AddValue("fixedWin", "window for fixed mode", fixedWin);
    cmd.AddValue("fixedRto", "RTO (s) for fixed mode", fixedRto);
    cmd.AddValue("maxWin", "max window for adaptive mode", maxWin);
    cmd.AddValue("corrupt", "probability a received packet gets a corrupted byte", corrupt);
    cmd.AddValue("trace", "write cwnd/rto trace csv", trace);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(run);
    g_N = (g_fileSize + g_payload - 1) / g_payload;

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(g_payload));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    NodeContainer nodes;
    nodes.Create(2);

    std::ostringstream rs, ds;
    rs << rateMbps << "Mbps";
    ds << delayMs << "ms";
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(rs.str()));
    p2p.SetChannelAttribute("Delay", StringValue(ds.str()));
    NetDeviceContainer devs = p2p.Install(nodes);

    if (loss > 0)
    {
        for (uint32_t i = 0; i < 2; i++)
        {
            Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
            em->SetAttribute("ErrorRate", DoubleValue(loss));
            em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
            devs.Get(i)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        }
    }

    InternetStackHelper stack;
    stack.Install(nodes);
    Ipv4AddressHelper addr;
    addr.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifc = addr.Assign(devs);

    const uint16_t port = 9000;
    Simulator::Stop(Seconds(2000));

    if (mode == "tcp")
    {
        PacketSinkHelper sink("ns3::TcpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sa = sink.Install(nodes.Get(1));
        sa.Start(Seconds(0.0));
        sa.Get(0)->TraceConnectWithoutContext("Rx", MakeCallback(&TcpRx));

        BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress(ifc.GetAddress(1), port));
        bulk.SetAttribute("MaxBytes", UintegerValue(g_fileSize));
        bulk.SetAttribute("SendSize", UintegerValue(g_payload));
        ApplicationContainer ca = bulk.Install(nodes.Get(0));
        ca.Start(Seconds(0.1));

        Simulator::Run();
        double t = g_tcpDone ? (g_tcpTime - 0.1) : -1;
        PrintResult("tcp", loss, delayMs, rateMbps, run, t, 0, -1, -1, g_tcpDone ? 1 : 0);
        Simulator::Destroy();
        return 0;
    }

    // deterministic pseudo-random "file"
    std::vector<uint8_t> file(g_fileSize);
    uint64_t x = 88172645463325252ULL;
    for (auto& c : file)
    {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        c = static_cast<uint8_t>(x >> 56);
    }

    std::ofstream tr;
    if (trace)
    {
        tr.open("rudp-trace-" + mode + ".csv");
        tr << "time,cwnd,rto,srtt,base\n";
    }

    bool adaptive = (mode == "adaptive");
    Receiver rx(nodes.Get(1), port, corrupt);
    Sender tx(nodes.Get(0),
              ifc.GetAddress(1),
              port,
              file,
              adaptive,
              fixedWin,
              fixedRto,
              maxWin,
              corrupt,
              trace ? &tr : nullptr);
    Simulator::Schedule(Seconds(0.0), &Receiver::Start, &rx);
    Simulator::Schedule(Seconds(0.1), &Sender::Start, &tx);

    Simulator::Run();

    bool intact = rx.complete && (Fnv(rx.m_buf) == Fnv(file));
    double t = tx.done ? (tx.doneTime.GetSeconds() - 0.1) : -1;
    PrintResult(mode,
                loss,
                delayMs,
                rateMbps,
                run,
                t,
                tx.dataSent,
                tx.retx,
                tx.timeouts,
                intact ? 1 : 0);
    std::cerr << "[info] bad(checksum)=" << rx.badPkts << " dup=" << rx.dups
              << " out-of-order=" << rx.outOfOrder << std::endl;
    Simulator::Destroy();
    return 0;
}
