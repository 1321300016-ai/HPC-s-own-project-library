#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSerialPort/QSerialPort>
#include <QVector>
#include <QPointF>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

namespace Ui {
class MainWindow;
}

/*
 * 电机串口监控：USB 连接 STM32 ->
 *   1) 发送 rpm/kp/ki/dsp 等命令
 *   2) 解析固件回传的 $D,ms,A,B,target,PWM_A,PWM_B 数据帧
 *   3) QtCharts 实时绘制 A/B/目标三条滚动曲线
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    void setupChart();                     /* 创建左右两张图 */
    void refreshPorts();                   /* 扫描可用串口 */
    void sendCommand(const QString &text); /* 发一条命令 */
    void onReadyRead();                    /* 收到串口数据 */
    void parseFrame(const QString &line);  /* 解析 $D 帧 */
    void appendLog(const QString &text);   /* 写日志 */
    void saveCsv();                        /* 把当前波形存成 CSV */
    void runStepAnalysis();                /* 阶跃响应指标计算 */

    Ui::MainWindow *ui;

    QSerialPort  *m_port = Q_NULLPTR;
    QTimer       *m_refreshTimer = Q_NULLPTR;
    QTimer       *m_chartTimer = Q_NULLPTR;  /* 图表刷新定时器：200ms 一次 */
    QByteArray    m_rxBuf;
    qint64        m_rxBytes = 0;      /* 已收到的总字节数，用于判断串口是否真的通 */

    void updateChart();               /* 把累积的数据画到左右两张图 */
    QtCharts::QChartView   *m_chartViewA = Q_NULLPTR;  /* 左图：A 电机 */
    QtCharts::QChart       *m_chartA     = Q_NULLPTR;
    QtCharts::QLineSeries  *m_seriesA    = Q_NULLPTR;  /* A 实测 */
    QtCharts::QLineSeries  *m_seriesTA   = Q_NULLPTR;  /* 目标（A 图） */
    QtCharts::QValueAxis   *m_axisXA     = Q_NULLPTR;
    QtCharts::QValueAxis   *m_axisYA     = Q_NULLPTR;

    QtCharts::QChartView   *m_chartViewB = Q_NULLPTR;  /* 右图：B 电机 */
    QtCharts::QChart       *m_chartB     = Q_NULLPTR;
    QtCharts::QLineSeries  *m_seriesB    = Q_NULLPTR;  /* B 实测 */
    QtCharts::QLineSeries  *m_seriesTB   = Q_NULLPTR;  /* 目标（B 图） */
    QtCharts::QValueAxis   *m_axisXB     = Q_NULLPTR;
    QtCharts::QValueAxis   *m_axisYB     = Q_NULLPTR;

    QVector<QPointF> m_ptsA, m_ptsB, m_ptsT;
    qint64      m_t0        = -1;
    qreal       m_lastPwmA  = 0;      /* 最新一帧 PWM_A，用于底部数值显示 */
    qreal       m_lastPwmB  = 0;      /* 最新一帧 PWM_B */
    bool        m_fitAll    = false;  /* true=显示全部历史波形（点数据流关后） */
    const qreal m_windowSec = 20.0;
    const int   m_maxPoints = 12000;  /* 最多保留约 4 分钟数据，供查看全貌 */
};

#endif // MAINWINDOW_H
