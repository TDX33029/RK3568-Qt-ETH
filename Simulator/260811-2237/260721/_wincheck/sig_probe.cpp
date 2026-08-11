/* 信号链最小验证: EthReader 收帧 -> emit frameReceived -> queued -> 槽 (本机桩环境) */
#include "eth_reader.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

static void on_frame(const Tracker3DMeasurement &m, double dt, quint16 seq) {
    fprintf(stderr, "[PROBE] frame seq=%u dt=%.3f tdoa0=%d\n", seq, dt, m.has_tdoa[0]);
    fflush(stderr);
}

static void on_error(const QString &msg) {
    fprintf(stderr, "[PROBE] ERROR: %s\n", qPrintable(msg));
    fflush(stderr);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qRegisterMetaType<Tracker3DMeasurement>();

    EthReader reader;
    reader.setHost("127.0.0.1");
    reader.setPort(5000);
    /* 诊断模式 1: AutoConnection (对象 affinity 同在主线程 -> DirectConnection)
     * 若 frame 到达 => emit 正常, 问题在 QueuedConnection/metatype 路径;
     * 若 frame 不到 => 数据帧在校验环节被丢弃。 */
    QObject::connect(&reader, &EthReader::frameReceived, &on_frame);
    QObject::connect(&reader, &EthReader::error, &on_error);
    reader.start();

    QTimer::singleShot(2500, &app, &QCoreApplication::quit);
    int rc = app.exec();
    reader.stop();
    reader.wait();
    fprintf(stderr, "[PROBE] quit rc=%d ping=%u\n", rc, reader.pingCount());
    return rc;
}
