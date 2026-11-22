#include "telemetry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <algorithm>
#include <cmath>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace {
constexpr double gib = 1024.0 * 1024 * 1024;

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

double number(const QString &path)
{
    bool ok = false;
    const double n = read(path).trimmed().toDouble(&ok);
    return ok ? n : unavailable;
}

double maximum(double a, double b)
{
    return std::isnan(a) ? b : std::isnan(b) ? a : std::max(a, b);
}

QList<QByteArray> words(const QByteArray &line)
{
    return line.simplified().split(' ');
}

QString label(const QString &path)
{
    return QString::fromUtf8(read(path).trimmed());
}

QHash<quint64, quint64> gpuTimes(const QByteArray &processPath)
{
    QHash<quint64, quint64> times;
    const QByteArray descriptors = processPath + "/fd/";
    DIR *dir = opendir(descriptors.constData());
    if (!dir)
        return times;
    while (const dirent *entry = readdir(dir)) {
        if (entry->d_name[0] == '.')
            continue;
        const QByteArray fd = entry->d_name;
        char target[128];
        const ssize_t len = readlink((descriptors + fd).constData(), target, sizeof target);
        if (len < 9 || QByteArrayView(target, len).first(9) != QByteArrayView("/dev/dri/", 9))
            continue;
        const auto info = read(QString::fromLatin1(processPath + "/fdinfo/" + fd));
        if (!info.contains("drm-driver:\tmsm") && !info.contains("drm-driver: msm"))
            continue;
        quint64 id = 0, busy = 0;
        bool haveId = false, haveBusy = false;
        for (const auto &line : info.split('\n')) {
            if (line.startsWith("drm-client-id:"))
                id = line.sliced(14).trimmed().toULongLong(&haveId);
            if (line.startsWith("drm-engine-gpu:"))
                busy = words(line.sliced(15)).value(0).toULongLong(&haveBusy);
        }
        if (haveId && haveBusy)
            times[id] = std::max(times.value(id), busy);
    }
    closedir(dir);
    return times;
}

double rate(quint64 previous, quint64 current, double seconds, double unit = 1)
{
    return current >= previous && seconds > 0 ? (current - previous) * unit / seconds : unavailable;
}
}

Telemetry::Telemetry()
{
    clock.start();
    pageSize = sysconf(_SC_PAGESIZE);
    struct stat root{};
    if (stat("/", &root) == 0) {
        rootMajor = major(root.st_dev);
        rootMinor = minor(root.st_dev);
    }
    const QDir devfreq("/sys/class/devfreq");
    for (const auto &name : devfreq.entryList({"*.gpu", "*kgsl*"}, QDir::Dirs))
        if (QFileInfo::exists(devfreq.filePath(name + "/cur_freq"))) {
            gpuPath = devfreq.filePath(name);
            break;
        }

    const QDir thermal("/sys/class/thermal");
    for (const auto &name : thermal.entryList({"thermal_zone*"}, QDir::Dirs)) {
        const auto path = thermal.filePath(name);
        thermalTypes.insert(path, label(path + "/type"));
    }

    const QDir power("/sys/class/power_supply");
    for (const auto &name : power.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = power.filePath(name);
        const auto type = label(path + "/type");
        if (type == "Battery" && (batteryPath.isEmpty() || name == "book4-ec-battery"))
            batteryPath = path;
        if (type == "Mains" && (acPath.isEmpty() || name == "book4-ec-ac"))
            acPath = path;
    }
}

Sample Telemetry::sample()
{
    Sample result;
    const qint64 now = clock.nsecsElapsed();
    const double seconds = lastSample ? (now - lastSample) / 1e9 : 0;
    lastSample = now;
    quint64 systemTicks = 0;
    QHash<int, CpuTime> cpuTimes;
    QHash<int, double> load;
    for (const auto &line : read("/proc/stat").split('\n')) {
        if (!line.startsWith("cpu"))
            break;
        const auto fields = words(line);
        if (fields.size() < 9)
            continue;
        const int id = fields[0] == "cpu" ? -1 : fields[0].mid(3).toInt();
        CpuTime time;
        for (int i = 1; i <= 8; ++i)
            time.total += fields[i].toULongLong();
        time.idle = fields[4].toULongLong() + fields[5].toULongLong();
        cpuTimes.insert(id, time);
        const CpuTime previous = lastCpu.value(id, time);
        const quint64 delta = time.total > previous.total ? time.total - previous.total : 0;
        const quint64 idle = time.idle > previous.idle ? time.idle - previous.idle : 0;
        load.insert(id, delta ? 100.0 * (delta - std::min(delta, idle)) / delta : unavailable);
        if (id == -1)
            systemTicks = delta;
    }
    lastCpu = cpuTimes;
    result.cpu = load.value(-1, unavailable);
    const QDir cpufreq("/sys/devices/system/cpu/cpufreq");
    const double turbo = number(cpufreq.filePath("boost"));
    result.turboKnown = !std::isnan(turbo);
    result.turbo = turbo == 1;
    for (const auto &policy : cpufreq.entryList({"policy*"}, QDir::Dirs)) {
        const auto path = cpufreq.filePath(policy);
        const int id = policy.sliced(6).toInt();
        const double mhz = number(path + "/scaling_cur_freq") / 1000;
        result.peakMhz = maximum(result.peakMhz, mhz);
        if (result.governor.isEmpty())
            result.governor = label(path + "/scaling_governor");
        for (const auto &cpu : words(read(path + "/related_cpus"))) {
            bool ok = false;
            const int core = cpu.toInt(&ok);
            if (ok && load.contains(core))
                result.cores.append({core, id, mhz, load.value(core)});
        }
    }
    std::sort(result.cores.begin(), result.cores.end(), [](const Core &a, const Core &b) { return a.id < b.id; });

    for (auto t = thermalTypes.cbegin(); t != thermalTypes.cend(); ++t) {
        if (t.value().startsWith("cpu") || t.value().startsWith("gpuss")) {
            const double degrees = number(t.key() + "/temp") / 1000;
            if (t.value().startsWith("cpu"))
                result.cpuTemp = maximum(result.cpuTemp, degrees);
            else
                result.gpuTemp = maximum(result.gpuTemp, degrees);
        }
    }
    if (!gpuPath.isEmpty()) {
        result.gpuMhz = number(gpuPath + "/cur_freq") / 1e6;
        result.gpuMinMhz = number(gpuPath + "/min_freq") / 1e6;
        result.gpuMaxMhz = number(gpuPath + "/max_freq") / 1e6;
        result.gpuGovernor = label(gpuPath + "/governor");
    }

    QHash<int, ProcessTime> processes;
    QHash<quint64, quint64> gpu;
    const QDir proc("/proc");
    for (const auto &name : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort)) {
        bool ok = false;
        const int pid = name.toInt(&ok);
        if (!ok)
            continue;
        const QString path = proc.filePath(name);
        const auto stat = read(path + "/stat");
        const auto start = stat.indexOf('(');
        const auto end = stat.lastIndexOf(')');
        if (start < 0 || end <= start || end + 2 >= stat.size())
            continue;
        const auto fields = words(stat.sliced(end + 2));
        if (fields.size() < 22)
            continue;
        const quint64 ticks = fields[11].toULongLong() + fields[12].toULongLong();
        const quint64 started = fields[19].toULongLong();
        processes.insert(pid, {ticks, started});
        const ProcessTime previous = lastProcesses.value(pid, {ticks, started});
        const double cpu = systemTicks && previous.start == started && ticks >= previous.ticks
            ? std::min(100.0, 100.0 * (ticks - previous.ticks) / systemTicks) : unavailable;
        const qlonglong resident = fields[21].toLongLong();
        result.processes.append({pid, QString::fromUtf8(stat.sliced(start + 1, end - start - 1)), cpu,
                                 pageSize > 0 && resident >= 0 ? resident * (pageSize / 1048576.0) : unavailable});

        const auto clients = gpuTimes(path.toLatin1());
        for (auto gpuClient = clients.cbegin(); gpuClient != clients.cend(); ++gpuClient)
            gpu[gpuClient.key()] = std::max(gpu.value(gpuClient.key()), gpuClient.value());
    }
    lastProcesses = processes;
    double busiest = 0;
    bool measured = false;
    for (auto entry = gpu.cbegin(); entry != gpu.cend(); ++entry) {
        const auto previous = lastGpu.constFind(entry.key());
        if (previous == lastGpu.cend())
            continue;
        const double busy = rate(previous.value(), entry.value(), seconds, 1e-9);
        if (std::isfinite(busy)) {
            measured = true;
            busiest += busy;
        }
    }
    if (measured)
        result.gpu = std::clamp(busiest * 100, 0.0, 100.0);
    lastGpu = gpu;

    QHash<QByteArray, double> memory;
    for (const auto &line : read("/proc/meminfo").split('\n')) {
        const auto fields = words(line);
        if (fields.size() >= 2)
            memory.insert(fields[0], fields[1].toDouble() / (1024 * 1024));
    }
    result.ramTotalGiB = memory.value("MemTotal:", unavailable);
    result.ramGiB = result.ramTotalGiB - memory.value("MemAvailable:", unavailable);
    result.swapTotalGiB = memory.value("SwapTotal:", unavailable);
    result.swapGiB = result.swapTotalGiB - memory.value("SwapFree:", unavailable);

    if (!batteryPath.isEmpty()) {
        result.battery = number(batteryPath + "/capacity");
        result.batteryStatus = label(batteryPath + "/status");
        result.watts = number(batteryPath + "/power_now") / 1e6;
        if (std::isnan(result.watts))
            result.watts = std::abs(number(batteryPath + "/current_now") * number(batteryPath + "/voltage_now")) / 1e12;
    }
    if (!acPath.isEmpty()) {
        const double ac = number(acPath + "/online");
        result.acKnown = std::isfinite(ac);
        result.onAc = ac == 1;
    }

    const auto storage = QStorageInfo::root();
    if (storage.isValid() && storage.bytesTotal() > 0 && storage.bytesAvailable() >= 0) {
        result.diskTotalGiB = storage.bytesTotal() / gib;
        result.diskGiB = (storage.bytesTotal() - storage.bytesAvailable()) / gib;
    }
    bool haveDisk = false;
    for (const auto &line : read("/proc/diskstats").split('\n')) {
        const auto f = words(line);
        if (f.size() < 10 || f[0].toUInt() != rootMajor || f[1].toUInt() != rootMinor)
            continue;
        const Io counters{f[5].toULongLong(), f[9].toULongLong()};
        if (diskKnown) {
            result.readSpeed = rate(lastDisk.first, counters.first, seconds, 512);
            result.writeSpeed = rate(lastDisk.second, counters.second, seconds, 512);
        }
        lastDisk = counters;
        haveDisk = true;
        break;
    }
    diskKnown = haveDisk;

    QHash<QString, Io> interfaces;
    for (const auto &line : read("/proc/net/dev").split('\n')) {
        const auto delimiter = line.indexOf(':');
        if (delimiter == -1 || line.sliced(delimiter + 1).isEmpty())
            continue;
        const QString device = QString::fromLatin1(line.first(delimiter).trimmed());
        const auto data = words(line.sliced(delimiter + 1));
        if (device != "lo" && data.size() >= 9)
            interfaces.insert(device, {data[0].toULongLong(), data[8].toULongLong()});
    }
    unsigned metric = std::numeric_limits<unsigned>::max();
    for (const auto &line : read("/proc/net/route").split('\n')) {
        const auto fields = words(line);
        if (fields.size() >= 8 && fields[1] == "00000000" && (fields[3].toUInt(nullptr, 16) & 1)
            && fields[6].toUInt() < metric) {
            const QString device = QString::fromLatin1(fields[0]);
            if (interfaces.contains(device)) {
                result.network = device;
                metric = fields[6].toUInt();
            }
        }
    }
    if (result.network.isEmpty() && !interfaces.isEmpty()) {
        auto names = interfaces.keys();
        names.sort();
        for (const auto &name : names)
            if (label("/sys/class/net/" + name + "/operstate") == "up") {
                result.network = name;
                break;
            }
    }
    if (interfaces.contains(result.network) && lastNetwork.contains(result.network)) {
        const auto current = interfaces.value(result.network);
        const auto previous = lastNetwork.value(result.network);
        result.rxSpeed = rate(previous.first, current.first, seconds);
        result.txSpeed = rate(previous.second, current.second, seconds);
    }
    lastNetwork = interfaces;
    return result;
}
