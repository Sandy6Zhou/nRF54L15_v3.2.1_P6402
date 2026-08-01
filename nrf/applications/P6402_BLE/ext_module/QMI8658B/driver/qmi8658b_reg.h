/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        qmi8658b_reg.h
**文件描述:        QMI8658B 芯片寄存器和控制位定义头文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.07.30
*********************************************************************
** 功能描述:        定义 QMI8658B 寄存器地址、控制位、命令码和驱动阈值
*********************************************************************/

#ifndef _QMI8658B_REG_H_
#define _QMI8658B_REG_H_

#define QMI8658B_REG_WHO_AM_I             0x00U
#define QMI8658B_REG_CTRL1                0x02U
#define QMI8658B_REG_CTRL2                0x03U
#define QMI8658B_REG_CTRL3                0x04U
#define QMI8658B_REG_CTRL5                0x06U
#define QMI8658B_REG_CTRL7                0x08U
#define QMI8658B_REG_CTRL8                0x09U
#define QMI8658B_REG_CTRL9                0x0AU
#define QMI8658B_REG_CAL1_L               0x0BU
#define QMI8658B_REG_FIFO_WATERMARK       0x13U
#define QMI8658B_REG_FIFO_CTRL            0x14U
#define QMI8658B_REG_FIFO_COUNT           0x15U
#define QMI8658B_REG_FIFO_STATUS          0x16U
#define QMI8658B_REG_FIFO_DATA            0x17U
#define QMI8658B_REG_STATUS_INT           0x2DU
#define QMI8658B_REG_STATUS0              0x2EU
#define QMI8658B_REG_STATUS1              0x2FU
#define QMI8658B_REG_TIMESTAMP_L          0x30U
#define QMI8658B_REG_TEMP_L               0x33U
#define QMI8658B_REG_AX_L                 0x35U
#define QMI8658B_REG_GX_L                 0x3BU
#define QMI8658B_REG_COD_STATUS           0x46U
#define QMI8658B_REG_DQW_L                0x49U
#define QMI8658B_REG_DVX_L                0x51U
#define QMI8658B_REG_TAP_STATUS           0x59U
#define QMI8658B_REG_RESET                0x60U
#define QMI8658B_REG_RESET_DONE           0x4DU

#define QMI8658B_CHIP_ID                  0x05U

/* CTRL1 位定义 (0x02) */
#define QMI8658B_CTRL1_ADDR_AI            0x40U
#define QMI8658B_CTRL1_SENSOR_DISABLE     0x01U
#define QMI8658B_CTRL1_INT1_ENABLE        0x08U
#define QMI8658B_CTRL1_FIFO_INT1          0x04U

/* CTRL5 低通滤波器配置 */
#define QMI8658B_CTRL5_ACC_LPF_EN         0x01U
#define QMI8658B_CTRL5_GYR_LPF_EN         0x10U

/* CTRL7 位定义 (0x08) */
#define QMI8658B_CTRL7_ACC_ENABLE         0x01U
#define QMI8658B_CTRL7_GYR_ENABLE         0x02U
#define QMI8658B_CTRL7_GYR_SNOOZE         0x10U
#define QMI8658B_CTRL7_DRDY_DISABLE       0x20U
#define QMI8658B_CTRL7_SYNC_SAMPLE        0x80U

/* CTRL8 位定义 (0x09) */
#define QMI8658B_CTRL8_TAP_ENABLE         0x01U
#define QMI8658B_CTRL8_ANY_MOTION_ENABLE  0x02U
#define QMI8658B_CTRL8_NO_MOTION_ENABLE   0x04U
#define QMI8658B_CTRL8_SIG_MOTION_ENABLE  0x08U
#define QMI8658B_CTRL8_INT_SEL            0x40U
#define QMI8658B_CTRL8_HANDSHAKE          0x80U

/* STATUS0 位定义 (0x2E) */
#define QMI8658B_STATUS0_ACC_DRDY         0x01U
#define QMI8658B_STATUS0_GYR_DRDY         0x02U

/* STATUS1 位定义 (0x2F) */
#define QMI8658B_STATUS1_TAP              0x02U
#define QMI8658B_STATUS1_ANY_MOTION       0x20U
#define QMI8658B_STATUS1_NO_MOTION        0x40U
#define QMI8658B_STATUS1_SIG_MOTION       0x80U

/* STATUSINT 位定义 (0x2D) */
#define QMI8658B_STATUS_INT_CMD_DONE      0x80U

/* FIFO 配置 */
#define QMI8658B_FIFO_CTRL_SIZE_16        0x00U
#define QMI8658B_FIFO_CTRL_SIZE_32        0x04U
#define QMI8658B_FIFO_CTRL_SIZE_64        0x08U
#define QMI8658B_FIFO_CTRL_SIZE_128       0x0CU

/* FIFO_STATUS 位定义 (0x16) */
#define QMI8658B_FIFO_STATUS_NOT_EMPTY    0x10U
#define QMI8658B_FIFO_STATUS_FULL         0x80U
#define QMI8658B_FIFO_STATUS_WTM          0x40U
#define QMI8658B_FIFO_STATUS_OVFLOW       0x20U

/* CTRL9 命令码 */
#define QMI8658B_CMD_ACK                  0x00U
#define QMI8658B_CMD_RESET_FIFO           0x04U
#define QMI8658B_CMD_REQUEST_FIFO         0x05U
#define QMI8658B_CMD_ACC_OFFSET           0x09U
#define QMI8658B_CMD_GYR_OFFSET           0x0AU
#define QMI8658B_CMD_ENABLE_TAP           0x0CU
#define QMI8658B_CMD_MOTION               0x0EU
#define QMI8658B_CMD_COPY_USID            0x10U
#define QMI8658B_CMD_AHB_CLOCK_GATING     0x12U
#define QMI8658B_CMD_ON_DEMAND_CALI       0xA2U
#define QMI8658B_CMD_APPLY_GYRO_GAIN      0xAAU

/* 自检阈值 */
#define QMI8658B_ST_ACC_THRESHOLD_MG      200
#define QMI8658B_ST_GYR_THRESHOLD_MDPS    300000

/* 驱动内部超时 */
#define QMI8658B_COMMAND_TIMEOUT_MS       100U
#define QMI8658B_DRDY_TIMEOUT_MS          500U
#define QMI8658B_CALIBRATION_TIME_MS      2200U

#endif /* _QMI8658B_REG_H_ */
