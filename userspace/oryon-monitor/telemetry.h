#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>
#include <limits>

inline constexpr double unavailable = std::numeric_limits<double>::quiet_NaN();

struct Core {
    int id;
    int policy;
    double mhz;
    double load;
};

struct Process {
    int pid;
    QString name;
    double cpu;
    double ramMiB;
};

struct Sample {
    QList<Core> cores;
    QList<Process> processes;
    double cpu = unavailable, cpuTemp = unavailable, peakMhz = unavailable;
    double gpu = unavailable, gpuTemp = unavailable, gpuMhz = unavailable;
    double gpuMinMhz = unavailable, gpuMaxMhz = unavailable;
    double ramGiB = unavailable, ramTotalGiB = unavailable;
    double swapGiB = unavailable, swapTotalGiB = unavailable;
    double battery = unavailable, watts = unavailable;
    double diskGiB = unavailable, diskTotalGiB = unavailable;
    double readSpeed = unavailable, writeSpeed = unavailable;
    double rxSpeed = unavailable, txSpeed = unavailable;
    QString governor, gpuGovernor, batteryStatus, network;
    bool turbo = false, turboKnown = false, onAc = false, acKnown = false;
};

class Telemetry {
public:
    Telemetry();
    Sample sample();

private:
    struct CpuTime { quint64 total = 0, idle = 0; };
    struct ProcessTime { quint64 ticks, start; };
    struct Io { quint64 first, second; };
    QElapsedTimer clock;
    qint64 lastSample = 0;
    unsigned rootMajor = 0, rootMinor = 0;
    long pageSize = 0;
    QHash<int, CpuTime> lastCpu;
    QHash<int, ProcessTime> lastProcesses;
    QHash<quint64, quint64> lastGpu;
    QHash<QString, Io> lastNetwork;
    Io lastDisk{};
    bool diskKnown = false;
    QString gpuPath, batteryPath, acPath;
    QHash<QString, QString> thermalTypes;
};
