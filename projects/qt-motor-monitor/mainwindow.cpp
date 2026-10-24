#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QTimer>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QSerialPortInfo>
#include <QVBoxLayout>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextCursor>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

using namespace QtCharts;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_port(new QSerialPort(this))
{
    ui->setupUi(this);
    setWindowTitle("双电机 PID 调速监控");

    setupChart();

    /* 波特率默认 115200（固件串口） */
    ui->comboBaud->setCurrentText("115200");

    /* 每 2 秒扫描一次串口 */
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->start(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshPorts);
    refreshPorts();

    /* 图表 100ms 刷新一次（10Hz）：
       串口 20ms 一帧，若每帧都全量重画 3 条曲线，界面会被拖死 */
    m_chartTimer = new QTimer(this);
    m_chartTimer->start(200);   /* 5Hz：线条变化平滑、界面轻量 */
    connect(m_chartTimer, &QTimer::timeout, this, &MainWindow::updateChart);

    /* 打开 / 关闭串口 */
    connect(ui->btnOpen, &QPushButton::clicked, this, [this]() {
        if (m_port->isOpen())
        {
            m_port->close();
            ui->btnOpen->setText("打开串口");
            ui->lblStatus->setText("已关闭");
            ui->lblStatus->setStyleSheet("color:gray;");
            appendLog("串口已关闭");
            return;
        }

        QString portName = ui->comboPort->currentText();
        if (portName.isEmpty())
        {
            ui->lblStatus->setText("没有可用串口");
            return;
        }
        m_port->setPortName(portName);
        m_port->setBaudRate(ui->comboBaud->currentText().toInt());
        m_port->setDataBits(QSerialPort::Data8);
        m_port->setParity(QSerialPort::NoParity);
        m_port->setStopBits(QSerialPort::OneStop);
        m_port->setFlowControl(QSerialPort::NoFlowControl);

        if (m_port->open(QIODevice::ReadWrite))
        {
            m_rxBytes = 0;
            ui->btnOpen->setText("关闭串口");
            ui->lblStatus->setText("已连接 " + portName + "  RX=0");
            ui->lblStatus->setStyleSheet("color:#00aa00;");
            appendLog("串口已打开：" + portName);
        }
        else
        {
            ui->lblStatus->setText("打开失败");
            appendLog("串口打开失败：" + m_port->errorString());
        }
    });

    /* 设备被拔掉等错误处理 */
    connect(m_port,
            static_cast<void (QSerialPort::*)(QSerialPort::SerialPortError)>(
                &QSerialPort::errorOccurred),
            this, [this](QSerialPort::SerialPortError err) {
        if (err == QSerialPort::ResourceError && m_port->isOpen())
        {
            appendLog("串口错误：" + m_port->errorString());
            m_port->close();
            ui->btnOpen->setText("打开串口");
            ui->lblStatus->setText("已断开");
            ui->lblStatus->setStyleSheet("color:red;");
        }
    });

    connect(m_port, &QSerialPort::readyRead, this, &MainWindow::onReadyRead);

    /* 刷新串口列表 */
    connect(ui->btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshPorts);

    /* 发送：按钮、回车、快捷按钮 */
    connect(ui->btnSend, &QPushButton::clicked, this, [this]() {
        sendCommand(ui->editCmd->text());
    });
    connect(ui->editCmd, &QLineEdit::returnPressed, this, [this]() {
        sendCommand(ui->editCmd->text());
    });

    const char *quick[][2] = {
        {"停止", "rpm 0"},
        {"40rpm", "rpm 40"},
        {"60rpm", "rpm 60"},
        {"A开环500", "pwm a 500"},
        {"数据流开", "dsp 1"},
        {"数据流关", "dsp 0"}
    };
    for (int i = 0; i < 6; i++)
    {
        QPushButton *b = new QPushButton(QString::fromUtf8(quick[i][0]), this);
        QString cmd = QString::fromUtf8(quick[i][1]);
        connect(b, &QPushButton::clicked, this, [this, cmd]() {
            sendCommand(cmd);
        });
        ui->layCmd->addWidget(b);
    }
    ui->layCmd->addStretch(1);

    /* 清空波形 */
    connect(ui->btnClear, &QPushButton::clicked, this, [this]() {
        m_ptsA.clear(); m_ptsB.clear(); m_ptsT.clear();
        m_t0 = -1;
        m_seriesA->clear(); m_seriesTA->clear();
        m_seriesB->clear(); m_seriesTB->clear();
        ui->lblNow->setText("A=--  B=--  目标=--  PWM_A=--  PWM_B=--");
        appendLog("波形已清空");
    });

    /* 保存 CSV + 阶跃分析 */
    connect(ui->btnSaveCsv, &QPushButton::clicked, this, &MainWindow::saveCsv);
    connect(ui->btnAnalyze, &QPushButton::clicked, this, &MainWindow::runStepAnalysis);
}

MainWindow::~MainWindow()
{
    if (m_port && m_port->isOpen())
        m_port->close();
    delete ui;
}

/* ============ 创建左右两张滚动曲线图 ============ */
void MainWindow::setupChart()
{
    /* ---------- 左图：电机 A ---------- */
    m_chartA = new QChart();
    m_chartA->setTitle("电机 A 转速 (rpm)");
    m_chartA->legend()->setVisible(true);
    m_chartA->legend()->setAlignment(Qt::AlignBottom);
    m_seriesA = new QLineSeries();
    m_seriesA->setName("A 实测");
    m_seriesA->setColor(QColor(220, 50, 50));
    QPen penA = m_seriesA->pen();
    penA.setWidth(3);
    m_seriesA->setPen(penA);
    m_seriesTA = new QLineSeries();
    m_seriesTA->setName("目标");
    m_seriesTA->setColor(QColor(230, 150, 20));
    QPen penTA = m_seriesTA->pen();
    penTA.setStyle(Qt::DashLine);
    penTA.setWidth(2);
    m_seriesTA->setPen(penTA);

    m_chartA->addSeries(m_seriesA);
    m_chartA->addSeries(m_seriesTA);
    m_axisXA = new QValueAxis();
    m_axisXA->setTitleText("时间 (s)");
    m_axisXA->setRange(0, 20);
    m_axisXA->setTickCount(6);
    m_axisYA = new QValueAxis();
    m_axisYA->setTitleText("rpm");
    m_axisYA->setRange(0, 160);
    m_axisYA->setTickCount(8);
    m_chartA->addAxis(m_axisXA, Qt::AlignBottom);
    m_chartA->addAxis(m_axisYA, Qt::AlignLeft);
    m_seriesA->attachAxis(m_axisXA);
    m_seriesA->attachAxis(m_axisYA);
    m_seriesTA->attachAxis(m_axisXA);
    m_seriesTA->attachAxis(m_axisYA);

    QVBoxLayout *layA = new QVBoxLayout(ui->chartHostA);
    layA->setContentsMargins(0, 0, 0, 0);
    m_chartViewA = new QChartView(m_chartA, ui->chartHostA);
    m_chartViewA->setRenderHint(QPainter::Antialiasing);
    layA->addWidget(m_chartViewA);

    /* ---------- 右图：电机 B ---------- */
    m_chartB = new QChart();
    m_chartB->setTitle("电机 B 转速 (rpm)");
    m_chartB->legend()->setVisible(true);
    m_chartB->legend()->setAlignment(Qt::AlignBottom);
    m_seriesB = new QLineSeries();
    m_seriesB->setName("B 实测");
    m_seriesB->setColor(QColor(50, 100, 220));
    QPen penB = m_seriesB->pen();
    penB.setWidth(3);
    m_seriesB->setPen(penB);
    m_seriesTB = new QLineSeries();
    m_seriesTB->setName("目标");
    m_seriesTB->setColor(QColor(230, 150, 20));
    QPen penTB = m_seriesTB->pen();
    penTB.setStyle(Qt::DashLine);
    penTB.setWidth(2);
    m_seriesTB->setPen(penTB);

    m_chartB->addSeries(m_seriesB);
    m_chartB->addSeries(m_seriesTB);
    m_axisXB = new QValueAxis();
    m_axisXB->setTitleText("时间 (s)");
    m_axisXB->setRange(0, 20);
    m_axisXB->setTickCount(6);
    m_axisYB = new QValueAxis();
    m_axisYB->setTitleText("rpm");
    m_axisYB->setRange(0, 160);
    m_axisYB->setTickCount(8);
    m_chartB->addAxis(m_axisXB, Qt::AlignBottom);
    m_chartB->addAxis(m_axisYB, Qt::AlignLeft);
    m_seriesB->attachAxis(m_axisXB);
    m_seriesB->attachAxis(m_axisYB);
    m_seriesTB->attachAxis(m_axisXB);
    m_seriesTB->attachAxis(m_axisYB);

    QVBoxLayout *layB = new QVBoxLayout(ui->chartHostB);
    layB->setContentsMargins(0, 0, 0, 0);
    m_chartViewB = new QChartView(m_chartB, ui->chartHostB);
    m_chartViewB->setRenderHint(QPainter::Antialiasing);
    layB->addWidget(m_chartViewB);
}

/* ============ 扫描串口 ============ */
void MainWindow::refreshPorts()
{
    QString cur = ui->comboPort->currentText();
    ui->comboPort->clear();
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos)
        ui->comboPort->addItem(info.portName());
    if (!cur.isEmpty())
        ui->comboPort->setCurrentText(cur);
}

/* ============ 发送命令 ============ */
void MainWindow::sendCommand(const QString &text)
{
    QString cmd = text.trimmed();
    if (cmd.isEmpty())
        return;
    if (!m_port || !m_port->isOpen())
    {
        appendLog("串口未打开，无法发送：" + cmd);
        return;
    }

    /* 点“数据流开”：恢复实时滚动；点“数据流关”：等板子停发后显示全貌 */
    QString lower = cmd.toLower();
    if (lower == "dsp 1")
    {
        m_fitAll = false;
    }
    else if (lower == "dsp 0")
    {
        QTimer::singleShot(300, this, [this]() {
            m_fitAll = true;
            updateChart();
        });
    }

    QByteArray out;
    if (ui->chkHex->isChecked())
    {
        QString hex = cmd;
        hex.remove(QRegularExpression("\\s+"));
        out = QByteArray::fromHex(hex.toLatin1());
    }
    else
    {
        out = cmd.toUtf8();
        /* msh 只有收到回车才会执行命令，文本模式永远自动补 \r\n */
        out += "\r\n";
    }
    m_port->write(out);
    appendLog("TX> " + cmd);
}

/* ============ 接收 ============ */
void MainWindow::onReadyRead()
{
    QByteArray data = m_port->readAll();
    m_rxBytes += data.size();
    ui->lblStatus->setText(QString("已连接 %1  RX=%2")
                               .arg(m_port->portName())
                               .arg(m_rxBytes));
    m_rxBuf += data;
    while (m_rxBuf.contains('\n'))
    {
        int idx = m_rxBuf.indexOf('\n');
        QByteArray line = m_rxBuf.left(idx);
        m_rxBuf.remove(0, idx + 1);
        line.replace("\r", "");

        /* msh 提示符有时会和 $D 帧拼在同一行（如 "msh >$D,123,..."），
           所以不要求 $D 在行首，只要行里出现就截出来解析 */
        QString s = QString::fromLatin1(line);
        int dpos = s.indexOf("$D,");
        if (dpos >= 0)
        {
            parseFrame(s.mid(dpos + 3));   /* 去掉 "$D," 再交给解析 */
        }
        else if (!s.isEmpty())
        {
            /* 把板子回的非数据文字（如 target rpm ok）显示出来，
               方便确认命令是否真的下到了板子 */
            QString rx = s.trimmed();
            if (!rx.isEmpty() && !rx.at(0).isDigit() && rx != "msh >")
                appendLog("RX> " + rx);
        }
    }
}

/* ============ 解析数据帧 ============ */
void MainWindow::parseFrame(const QString &line)
{
    QStringList f = line.split(',');
    /* 去掉 "$D," 前缀后，一行是 ms,A,B,target,pwmA,pwmB 共 6 个数 */
    if (f.size() < 6)
        return;

    bool okMs = false, okA = false, okB = false,
         okT = false, okPa = false, okPb = false;
    qint64 ms  = f.at(0).toLongLong(&okMs);
    qreal  a   = f.at(1).toDouble(&okA);
    qreal  b   = f.at(2).toDouble(&okB);
    qreal  tgt = f.at(3).toDouble(&okT);
    qreal  pa  = f.at(4).toDouble(&okPa);
    qreal  pb  = f.at(5).toDouble(&okPb);
    if (!(okMs && okA && okB && okT && okPa && okPb))
        return;
    m_lastPwmA = pa;
    m_lastPwmB = pb;

    if (m_t0 < 0)
        m_t0 = ms;
    qreal x = (ms - m_t0) / 1000.0;

    m_ptsA.append(QPointF(x, a));
    m_ptsB.append(QPointF(x, b));
    m_ptsT.append(QPointF(x, tgt));

    /* 只在超过最大点数时丢弃最旧点；平时保留完整数据，便于“看全貌” */
    while (!m_ptsA.isEmpty() && m_ptsA.size() > m_maxPoints)
        m_ptsA.removeFirst();
    while (!m_ptsB.isEmpty() && m_ptsB.size() > m_maxPoints)
        m_ptsB.removeFirst();
    while (!m_ptsT.isEmpty() && m_ptsT.size() > m_maxPoints)
        m_ptsT.removeFirst();

    /* 这里只存数据、不碰曲线，曲线交给 m_chartTimer 每 100ms 统一刷新 */
}

/* ============ 图表定时刷新 ============ */
void MainWindow::updateChart()
{
    if (m_ptsA.isEmpty() || m_ptsB.isEmpty() || m_ptsT.isEmpty())
        return;

    m_seriesA->replace(m_ptsA);
    m_seriesTA->replace(m_ptsT);      /* A 图里的目标曲线 */
    m_seriesB->replace(m_ptsB);
    m_seriesTB->replace(m_ptsT);      /* B 图里的目标曲线 */

    qreal firstX = m_ptsA.first().x();
    qreal lastX  = m_ptsA.last().x();
    qreal yMax = 0.0;
    qreal yMin = 0.0;

    if (m_fitAll)
    {
        /* 数据流已关：X/Y 都缩放到整段历史数据 */
        m_axisXA->setRange(firstX - 0.5, lastX + 2.0);
        m_axisXB->setRange(firstX - 0.5, lastX + 2.0);
        for (const QPointF &p : m_ptsA) { yMax = qMax(yMax, p.y()); yMin = qMin(yMin, p.y()); }
        for (const QPointF &p : m_ptsB) { yMax = qMax(yMax, p.y()); yMin = qMin(yMin, p.y()); }
        for (const QPointF &p : m_ptsT) { yMax = qMax(yMax, p.y()); yMin = qMin(yMin, p.y()); }
    }
    else
    {
        /* 实时模式：X 轴滚动最近 20 秒，Y 轴按最近 3 秒自动缩放 */
        m_axisXA->setRange(qMax(0.0, lastX - m_windowSec), qMax(m_windowSec, lastX + 1.0));
        m_axisXB->setRange(qMax(0.0, lastX - m_windowSec), qMax(m_windowSec, lastX + 1.0));
        for (int i = m_ptsA.size() - 1; i >= 0 && (lastX - m_ptsA.at(i).x()) <= 3.0; i--)
        {
            yMax = qMax(yMax, m_ptsA.at(i).y());
            yMin = qMin(yMin, m_ptsA.at(i).y());
        }
        for (int i = m_ptsB.size() - 1; i >= 0 && (lastX - m_ptsB.at(i).x()) <= 3.0; i--)
        {
            yMax = qMax(yMax, m_ptsB.at(i).y());
            yMin = qMin(yMin, m_ptsB.at(i).y());
        }
        for (int i = m_ptsT.size() - 1; i >= 0 && (lastX - m_ptsT.at(i).x()) <= 3.0; i--)
        {
            yMax = qMax(yMax, m_ptsT.at(i).y());
            yMin = qMin(yMin, m_ptsT.at(i).y());
        }
    }

    qreal upper = qMax(20.0, yMax * 1.2);
    qreal lower = qMin(0.0, yMin * 1.2 - 5.0);
    m_axisYA->setRange(lower, upper);
    m_axisYB->setRange(lower, upper);

    ui->lblNow->setText(QString("A=%1  B=%2  目标=%3  PWM_A=%4  PWM_B=%5")
                            .arg(m_ptsA.last().y(), 0, 'f', 0)
                            .arg(m_ptsB.last().y(), 0, 'f', 0)
                            .arg(m_ptsT.last().y(), 0, 'f', 0)
                            .arg(m_lastPwmA, 0, 'f', 0)
                            .arg(m_lastPwmB, 0, 'f', 0));
}

/* ============ 日志 ============ */
void MainWindow::appendLog(const QString &text)
{
    QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    ui->logEdit->appendPlainText(time + "  " + text);
    while (ui->logEdit->document()->blockCount() > 200)
    {
        QTextCursor c = ui->logEdit->textCursor();
        c.movePosition(QTextCursor::Start);
        c.select(QTextCursor::LineUnderCursor);
        c.removeSelectedText();
        c.deleteChar();
    }
}

/* ============ 保存 CSV ============ */
void MainWindow::saveCsv()
{
    if (m_ptsA.isEmpty() || m_ptsB.isEmpty() || m_ptsT.isEmpty())
    {
        appendLog("没有波形数据可保存，请先点“数据流开”录制");
        return;
    }

    QString file = QFileDialog::getSaveFileName(
                       this, "保存波形数据", "motor_step_data.csv", "CSV 文件 (*.csv)");
    if (file.isEmpty())
        return;

    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        appendLog("CSV 写入失败：" + f.errorString());
        return;
    }

    QTextStream out(&f);
    out << "time_s,A_rpm,B_rpm,target_rpm\r\n";
    int n = qMin(qMin(m_ptsA.size(), m_ptsB.size()), m_ptsT.size());
    for (int i = 0; i < n; i++)
    {
        out << QString::number(m_ptsA.at(i).x(), 'f', 3) << ","
            << QString::number(m_ptsA.at(i).y(), 'f', 1) << ","
            << QString::number(m_ptsB.at(i).y(), 'f', 1) << ","
            << QString::number(m_ptsT.at(i).y(), 'f', 1) << "\r\n";
    }
    f.close();
    appendLog("已保存 CSV：" + file);
}

/* ============ 阶跃响应分析 ============ */
void MainWindow::runStepAnalysis()
{
    const int n = m_ptsT.size();
    if (n < 30)
    {
        ui->lblAnalysis->setText("数据太少。请先点“数据流开”，发 rpm 0 稳定后，再发 rpm 60。");
        return;
    }

    /* 找最后一次目标转速发生跳变的位置（例如 0 -> 60） */
    int stepIdx = -1;
    for (int i = n - 1; i >= 1; i--)
    {
        if (qAbs(m_ptsT.at(i).y() - m_ptsT.at(i - 1).y()) >= 1.0)
        {
            stepIdx = i;
            break;
        }
    }
    if (stepIdx < 0 || n - stepIdx - 1 < 20)
    {
        ui->lblAnalysis->setText(
            "没找到完整的阶跃。请录制：rpm 0 停稳 2 秒 → rpm 60 跑到稳定 → 点“数据流关”后再分析。");
        return;
    }

    qreal tStep  = m_ptsT.at(stepIdx).x();
    qreal target = m_ptsT.at(stepIdx).y();

    /* 阶跃前的目标值：取跳变点前一个点的目标 */
    qreal baseTarget = m_ptsT.at(qMax(0, stepIdx - 1)).y();

    /* 对 A/B 各算一份指标 */
    QString lineA, lineB;
    int idx = 0;
    const QVector<QPointF> *motors[2] = {&m_ptsA, &m_ptsB};
    QString names[2] = {"A电机", "B电机"};
    for (idx = 0; idx < 2; idx++)
    {
        const QVector<QPointF> &pts = *motors[idx];

        /* 阶跃前基线：取阶跃点前最多 20 个点平均 */
        qreal baseSum = 0;
        int baseCnt = 0;
        for (int i = qMax(0, stepIdx - 20); i < stepIdx; i++)
        {
            baseSum += pts.at(i).y();
            baseCnt++;
        }
        qreal base = baseCnt ? baseSum / baseCnt : 0.0;

        /* 末尾稳定值：取最后最多 50 个点（约 1s）平均 */
        qreal endSum = 0;
        int endCnt = 0;
        for (int i = qMax(stepIdx, n - 50); i < n; i++)
        {
            endSum += pts.at(i).y();
            endCnt++;
        }
        qreal endVal = endCnt ? endSum / endCnt : base;

        if (target <= base + 0.5)
        {
            lineA += names[idx] + ": 不是上升阶跃，无法计算\r\n";
            continue;
        }

        qreal span = target - base;               /* 阶跃幅值 */
        qreal t10 = -1, t90 = -1;
        qreal peak = -1e9;
        for (int i = stepIdx; i < n; i++)
        {
            qreal y = pts.at(i).y();
            peak = qMax(peak, y);
            if (t10 < 0 && y >= base + 0.10 * span)
                t10 = pts.at(i).x();
            if (t90 < 0 && y >= base + 0.90 * span)
                t90 = pts.at(i).x();
        }

        qreal overshoot = 0;
        if (peak > target)
            overshoot = (peak - target) / span * 100.0;

        /* ±5% 目标带：从后往前找最后一次跑出带外的点 */
        qreal band = span * 0.05;
        qreal settleTime = -1;
        int lastOut = -1;
        for (int i = n - 1; i >= stepIdx; i--)
        {
            if (qAbs(pts.at(i).y() - target) > band)
            {
                lastOut = i;
                break;
            }
        }
        if (lastOut < 0)
            settleTime = 0;
        else if (lastOut < n - 1)
            settleTime = pts.at(lastOut + 1).x() - tStep;

        qreal steadyErr = endVal - target;
        QString s = names[idx] + ": 上升=";
        if (t10 >= 0 && t90 >= t10)
            s += QString::number(t90 - t10, 'f', 2) + "s";
        else
            s += "--";
        s += "  超调=" + QString::number(overshoot, 'f', 1) + "%";
        if (settleTime >= 0)
            s += "  调节=" + QString::number(settleTime, 'f', 2) + "s";
        else
            s += "  调节=未收敛";
        s += "  稳态误差=" + QString::number(steadyErr, 'f', 1) + "rpm\r\n";

        if (idx == 0)
            lineA = s;
        else
            lineB = s;
    }

    QString result = "检测到阶跃："
                     + QString::number(baseTarget, 'f', 0) + " → "
                     + QString::number(target, 'f', 0) + " rpm"
                     + "（t=" + QString::number(tStep, 'f', 2) + "s）\r\n";
    result += lineA + lineB;
    ui->lblAnalysis->setText(result);
    appendLog("阶跃分析完成");
}
