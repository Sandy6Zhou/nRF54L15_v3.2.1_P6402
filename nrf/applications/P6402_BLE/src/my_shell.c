/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_shell.c
**文件描述:        Shell 命令行交互模块实现（基于 RTT）
**当前版本:        V1.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.01.22
*********************************************************************
** 功能描述:        注册自定义 Shell 命令，用于系统诊断和设备控制
*********************************************************************/

/* 必须在包含 my_comm.h 之前定义 BLE_LOG_MODULE_ID，避免与 my_ble_log.h 中的默认定义冲突 */
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_SHELL

#include "my_comm.h"

#define LOG_MODULE_NAME my_shell
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

uint8_t g_shell_test_buff[256] = {0};

/********************************************************************
**函数名称:  cmd_system_info
**入口参数:  shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数:  无
**函数功能:  输出系统信息（示例命令）
**返 回 值:  0 表示成功
*********************************************************************/
static int cmd_system_info(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "=== System Information ===");
    shell_print(shell, "Device: nRF54L15");
    shell_print(shell, "Build Time: %s %s", __DATE__, __TIME__);
    shell_print(shell, "Uptime: %lld ms", k_uptime_get());
    return 0;
}

/********************************************************************
**函数名称:  cmd_ble_info
**入口参数:  shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数:  无
**函数功能:  输出蓝牙状态信息（示例命令）
**返 回 值:  0 表示成功
*********************************************************************/
static int cmd_ble_info(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "=== BLE Status ===");
    shell_print(shell, "Device Name: Harrison_UART_Service");
    shell_print(shell, "Advertising: Active");
    return 0;
}

/********************************************************************
**函数名称:  cmd_reboot
**入口参数:  shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数:  无
**函数功能:  系统重启命令
**返 回 值:  0 表示成功
*********************************************************************/
static int cmd_reboot(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "System rebooting...");
    k_sleep(K_MSEC(500));
    sys_reboot(SYS_REBOOT_WARM);
    return 0;
}

/********************************************************************
**函数名称:  my_shell_handle_rx
**入口参数:  pData    ---        接收到的数据缓冲区
**           iLen     ---        数据长度
**出口参数:  无
**函数功能:  处理接收到的字符串，解析并执行命令
**返 回 值:  无
*********************************************************************/
static void my_shell_handle_rx(uint8_t *pData, uint32_t iLen)
{
    static char command[MAX_CMD_LEN] = {0};
    static uint32_t index = 0;
    uint32_t i;

    for (i = 0; i < iLen; i++)
    {
        if (pData[i] == '\r' || pData[i] == '\n') // 回车是\r 为了兼容同时处理 \n
        {
            my_lte_parse_cmd(command, index);

            command[0] = 0;
            index = 0;

            // 如果下个字符是\n，跳过
            if (pData[i + 1] == '\n')
            {
                i++;
            }
        }
        else if (index < (MAX_CMD_LEN - 1))
        {
            command[index++] = pData[i];
            command[index] = '\0';
        }
    }
}

/********************************************************************
**函数名称:  shell_at_test
**入口参数:  sh       ---        shell结构体指针
**           argc     ---        参数个数
**           argv     ---        参数数组
**出口参数:  无
**函数功能:  AT测试命令处理函数
**返 回 值:  0表示成功，-EINVAL表示参数错误
*********************************************************************/
static int shell_at_test(const struct shell *sh, size_t argc, char **argv)
{
    int len;

    if (argc < 2)
    {
        shell_error(sh, "Missing parameter");
        return -EINVAL;
    }

    memset(g_shell_test_buff, 0, sizeof(g_shell_test_buff));

    len = strlen(argv[1]);
    memcpy(g_shell_test_buff, argv[1], len);
    // 手动增加\r\n，使得my_shell_handle_rx能识别到
    g_shell_test_buff[len++] = '\r';
    g_shell_test_buff[len++] = '\n';
    g_shell_test_buff[len] = 0;

    shell_print(sh, "param: %s, len: %d", argv[1], len);

    my_shell_handle_rx(g_shell_test_buff, len);

    return 0;
}

/********************************************************************
**函数名称：cmd_shutdown
**入口参数：shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数：无
**函数功能：执行系统关机（进入超低功耗模式，仅按键可唤醒）
**返 回 值：0 表示成功
*********************************************************************/
static int cmd_shutdown(const struct shell *shell, size_t argc, char **argv)
{
    msg_t msg;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(shell, "System shutdown request...");
    shell_print(shell, "Entering SHUTDOWN mode (ultra-low power, only KEY can wakeup)");

    /* 发送关机请求到主任务 */
    msg.msgID = MY_MSG_CTRL_SHUTDOWN_REQUEST;
    msg.pData = NULL;
    msg.DataLen = 0;
    my_send_msg_data(MOD_MAIN, MOD_MAIN, &msg);

    return 0;
}

/********************************************************************
**函数名称:  cmd_ble_log_test
**入口参数:  shell    ---        Shell 句柄
**           argc     ---        参数个数
**           argv     ---        参数数组
**出口参数:  无
**函数功能:  测试蓝牙日志发送功能
**返 回 值:  0 表示成功
**使用示例:  app blog "test message"
*********************************************************************/
static int cmd_ble_log_test(const struct shell *shell, size_t argc, char **argv)
{
    const char *msg;
    size_t len;
    uint8_t send_len;

    if (argc < 2)
    {
        shell_print(shell, "Usage: app blog \"<message>\"");
        shell_print(shell, "Example: app blog \"Hello BLE Log\"");
        shell_print(shell, "Note: Message must be enclosed in quotes");
        return -1;
    }

    msg = argv[1];
    len = strlen(msg);

    /* 检查参数是否包含空格（带引号的参数在argc=2时是一个整体）
     * 如果argc>2，说明参数被空格分割，用户可能忘记加引号 */
    if (argc > 2)
    {
        shell_print(shell, "Error: Message must be enclosed in quotes");
        shell_print(shell, "Usage: app blog \"<message>\"");
        return -1;
    }

    /* 检查长度限制 */
    if (len == 0)
    {
        shell_print(shell, "Message is empty");
        return -1;
    }

    if (len > 512)
    {
        shell_print(shell, "Message too long (max 512 bytes)");
        return -1;
    }

    /* 限制发送长度为 255 字节（ble_log_send 参数类型为 uint8_t） */
    send_len = (len > 255) ? 255 : (uint8_t)len;

    shell_print(shell, "Sending BLE log: %s", msg);
    ble_log_send((uint8_t *)msg, send_len);
    shell_print(shell, "BLE log sent, length: %d", send_len);

    return 0;
}

/********************************************************************
**函数名称:  cmd_ble_log_config
**入口参数:  shell    ---        Shell 句柄
**           argc     ---        参数个数
**           argv     ---        参数数组
**出口参数:  无
**函数功能:  蓝牙日志配置命令
**返 回 值:  0 表示成功
**使用示例:  app blogcfg global 1          (开启总开关)
**           app blogcfg mod BLE 1         (开启BLE模块)
**           app blogcfg level BLE 3       (BLE模块INF等级)
**           app blogcfg show              (显示配置)
*********************************************************************/
static int cmd_ble_log_config(const struct shell *shell, size_t argc, char **argv)
{
    ble_log_config_t *config;
    int ret;
    uint8_t level;
    uint8_t mod_id;
    uint8_t en;

    if (argc < 2)
    {
        shell_print(shell, "Usage:");
        shell_print(shell, "  app blogcfg global <0|1>        - Set global enable");
        shell_print(shell, "  app blogcfg mod <name> <0|1>    - Set module enable");
        shell_print(shell, "  app blogcfg level <name> <0-4>  - Set module level");
        shell_print(shell, "  app blogcfg show                - Show configuration");
        shell_print(shell, "Module names:");
        shell_print(shell, "  MAIN, BLE, DFU, SENSOR, LTE, CTRL, SHELL,");
        shell_print(shell, "  BATTERY, CMD, TOOL, PARAM, WDT, OTHER");
        shell_print(shell, "Level: 0=NONE, 1=ERR, 2=WRN, 3=INF, 4=DBG");
        return -1;
    }

    config = my_param_get_ble_log_config();

    if (strcmp(argv[1], "global") == 0)
    {
        if (argc < 3)
        {
            shell_print(shell, "Current global enable: %d", config->global_en);
            return 0;
        }

        en = atoi(argv[2]) ? 1 : 0; // 支持非0为1，否则为0

        ret = my_param_set_ble_log_global(en);  // 保存全局使能参数
        if (ret == 0)
        {
            shell_print(shell, "BLE log global enable set to: %d", en);
        }
        else
        {
            shell_print(shell, "Failed to set global enable");
        }
    }
    else if (strcmp(argv[1], "mod") == 0)
    {
        if (argc < 4)
        {
            shell_print(shell, "Usage: app blogcfg mod <name> <0|1>");
            return -1;
        }

        en = atoi(argv[3]) ? 1 : 0; // 支持非0为1，否则为0

        if (strcmp(argv[2], "MAIN") == 0)
            mod_id = BLE_LOG_MOD_MAIN;
        else if (strcmp(argv[2], "BLE") == 0) // BLE 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "BLE module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "DFU") == 0) // DFU 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "DFU module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "SENSOR") == 0)
            mod_id = BLE_LOG_MOD_SENSOR;
        else if (strcmp(argv[2], "LTE") == 0)
            mod_id = BLE_LOG_MOD_LTE;
        else if (strcmp(argv[2], "CTRL") == 0)
            mod_id = BLE_LOG_MOD_CTRL;
        else if (strcmp(argv[2], "SHELL") == 0) // SHELL 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "SHELL module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "BATTERY") == 0)
            mod_id = BLE_LOG_MOD_BATTERY;
        else if (strcmp(argv[2], "CMD") == 0) // CMD 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "CMD module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "TOOL") == 0)
            mod_id = BLE_LOG_MOD_TOOL;
        else if (strcmp(argv[2], "PARAM") == 0)
            mod_id = BLE_LOG_MOD_PARAM;
        else if (strcmp(argv[2], "WDT") == 0)
            mod_id = BLE_LOG_MOD_WDT;
        else if (strcmp(argv[2], "OTHER") == 0)
            mod_id = BLE_LOG_MOD_OTHER;
        else
        {
            shell_print(shell, "Unknown module: %s", argv[2]);
            return -1;
        }

        ret = my_param_set_ble_log_mod(mod_id, en);
        if (ret == 0)
        {
            shell_print(shell, "BLE log module %s enable set to: %d", argv[2], en);
        }
        else
        {
            shell_print(shell, "Failed to set module enable");
        }
    }
    else if (strcmp(argv[1], "level") == 0)
    {
        if (argc < 4)
        {
            shell_print(shell, "Usage: app blogcfg level <name> <0-4>");
            return -1;
        }

        level = atoi(argv[3]);

        if (strcmp(argv[2], "MAIN") == 0)
            mod_id = BLE_LOG_MOD_MAIN;
        else if (strcmp(argv[2], "BLE") == 0) // BLE 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "BLE module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "DFU") == 0) // DFU 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "DFU module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "SENSOR") == 0)
            mod_id = BLE_LOG_MOD_SENSOR;
        else if (strcmp(argv[2], "LTE") == 0)
            mod_id = BLE_LOG_MOD_LTE;
        else if (strcmp(argv[2], "CTRL") == 0)
            mod_id = BLE_LOG_MOD_CTRL;
        else if (strcmp(argv[2], "SHELL") == 0) // SHELL 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "SHELL module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "BATTERY") == 0)
            mod_id = BLE_LOG_MOD_BATTERY;
        else if (strcmp(argv[2], "CMD") == 0) // CMD 模块不支持 BLE 日志（递归风险），初始化时已禁用
        {
            shell_print(shell, "CMD module does not support BLE log (recursive risk)");
            return -1;
        }
        else if (strcmp(argv[2], "TOOL") == 0)
            mod_id = BLE_LOG_MOD_TOOL;
        else if (strcmp(argv[2], "PARAM") == 0)
            mod_id = BLE_LOG_MOD_PARAM;
        else if (strcmp(argv[2], "WDT") == 0)
            mod_id = BLE_LOG_MOD_WDT;
        else if (strcmp(argv[2], "OTHER") == 0)
            mod_id = BLE_LOG_MOD_OTHER;
        else
        {
            shell_print(shell, "Unknown module: %s", argv[2]);
            return -1;
        }

        ret = my_param_set_ble_log_level(mod_id, level);
        if (ret == 0)
        {
            shell_print(shell, "BLE log module %s level set to: %d", argv[2], level);
        }
        else
        {
            shell_print(shell, "Failed to set module level");
        }
    }
    else if (strcmp(argv[1], "show") == 0)
    {
        shell_print(shell, "BLE Log Configuration:");
        shell_print(shell, "  Global enable: %d", config->global_en);
        shell_print(shell, "  Module status (ON/OFF + level(0:NONE 1:ERR 2:WRN 3:INF 4:DBG)):");
        shell_print(shell, "    MAIN:   %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_MAIN) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_MAIN]);
        shell_print(shell, "    BLE:    %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_BLE) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_BLE]);
        shell_print(shell, "    DFU:    %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_DFU) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_DFU]);
        shell_print(shell, "    SENSOR: %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_SENSOR) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_SENSOR]);
        shell_print(shell, "    LTE:    %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_LTE) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_LTE]);
        shell_print(shell, "    CTRL:   %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_CTRL) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_CTRL]);
        shell_print(shell, "    SHELL:  %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_SHELL) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_SHELL]);
        shell_print(shell, "    BATTERY: %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_BATTERY) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_BATTERY]);
        shell_print(shell, "    CMD:    %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_CMD) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_CMD]);
        shell_print(shell, "    TOOL:   %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_TOOL) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_TOOL]);
        shell_print(shell, "    PARAM:  %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_PARAM) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_PARAM]);
        shell_print(shell, "    WDT:    %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_WDT) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_WDT]);
        shell_print(shell, "    OTHER:  %s  %d",
                    BLE_LOG_MOD_IS_ENABLED(config, BLE_LOG_MOD_OTHER) ? "ON" : "OFF",
                    config->mod_level[BLE_LOG_MOD_OTHER]);
    }
    else
    {
        shell_print(shell, "Unknown command: %s", argv[1]);
        return -1;
    }

    return 0;
}

/********************************************************************
**函数名称:  cmd_buzzer_test
**入口参数:  sh    ---        Shell句柄，用于输出信息
            argc  ---        参数个数
            argv  ---        参数数组，argv[1]为测试参数字符串
**出口参数:  无
**函数功能:  处理Buzzer测试命令，接收参数并发送测试消息到Buzzer模块
**返 回 值:  0表示成功
*********************************************************************/
static int cmd_buzzer_test(const struct shell *sh, size_t argc, char **argv)
{
    int len;

    if (argc < 2)
    {
        shell_error(sh, "Missing parameter");
        return -EINVAL;
    }

    memset(g_shell_test_buff, 0, sizeof(g_shell_test_buff));

    len = strlen(argv[1]);
    memcpy(g_shell_test_buff, argv[1], len);
    g_shell_test_buff[len] = 0;

    shell_print(sh, "param: %s, len: %d", argv[1], len);
    my_set_buzzer_mode(atoi(argv[1]));

    return 0;
}

/********************************************************************
**函数名称:  cmd_retransmit_check_test
**入口参数:  sh    ---   Shell 实例句柄
**           argc  ---   参数个数
**           argv  ---   参数数组 (argv[1]: 指令内容, argv[2]: 可选参数)
**出口参数:  无
**函数功能:  Shell 测试命令：手动触发 LTE 重传检查机制
**           调用 lte_send_cmd_with_retry 发送指令并启动重传
**指令格式:  retransmit_test [cmd_string] [optional_param]
**返回值说明:  0:      成功
**           -EINVAL: 参数缺失
**返 回 值:  int
*********************************************************************/
static int cmd_retransmit_check_test(const struct shell *sh, size_t argc, char **argv)
{
    int len;
    char *p = NULL;

    if (argc < 2)
    {
        shell_error(sh, "Missing parameter");
        return -EINVAL;
    }

    memset(g_shell_test_buff, 0, sizeof(g_shell_test_buff));

    len = strlen(argv[1]);
    memcpy(g_shell_test_buff, argv[1], len);
    g_shell_test_buff[len] = 0;

    // 检查是否有第二个参数
    if (argc >= 3)
    {
        p = argv[2];
        shell_print(sh, "param2: %s", p);
    }
    shell_print(sh, "param1: %s, len: %d", argv[1], len);

    lte_send_cmd_with_retry(argv[1], p);

    return 0;
}

/********************************************************************
**函数名称:  cmd_hardware_test
**入口参数:  sh    ---   Shell 实例句柄
**           argc  ---   参数个数
**           argv  ---   参数数组 (argv[1]: 指令内容, argv[2]: 可选参数)
**出口参数:  无
**函数功能:  Shell 测试命令：手动触发硬件测试
**指令格式:  hardware_ware [cmd_string] [optional_param]
**返回值说明:  0:      成功
**           -EINVAL: 参数缺失
**返 回 值:  int
*********************************************************************/
static int cmd_hardware_test(const struct shell *sh, size_t argc, char **argv)
{
    int mode = 0;

    // G-Sensor电源使能测试
    if (strcmp(argv[1], "gsensorpwren") == 0)
    {
        //gsensorpwren 0/1      (G-Sensor控制电源关/开)
        mode = atoi(argv[2]);

        my_gsensor_pwr_on(mode);
    }
    // 充电电路开关
    else if (strcmp(argv[1], "v_chg_en") == 0)
    {
        //v_chg_en 0/1      (充电电路关/开)
        mode = atoi(argv[2]);

        charge_enable(mode);
    }
    // 4G电源使能测试
    else if (strcmp(argv[1], "4GPOWER") == 0)
    {
        //4GPOWER 0/1      (4G控制电源关/开)
        mode = atoi(argv[2]);

        my_lte_pwr_on(mode);
    }

    return 0;
}


void cmd_read_gsensor_data(const struct shell *sh, size_t argc, char **argv)
{
    my_gsensor_data_t data;
    int ret;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    ret = my_gsensor_read_data(&data);
    if (ret != 0)
    {
        shell_error(sh, "QMI8658B read fail: %d", ret);
        return;
    }

    shell_print(sh, "ACC(mg): %d,%d,%d GYR(mdps): %d,%d,%d",
                data.acc_x_mg, data.acc_y_mg, data.acc_z_mg,
                data.gyr_x_mdps, data.gyr_y_mdps, data.gyr_z_mdps);
}

/* ==================== IO 测试命令 ====================
 * 通过宏 IO_TEST_ENABLE 控制：1 启用，0 或删除本区块后禁用
 * 测试完成后可将 IO_TEST_ENABLE 置 0 或整体删除本区块
 * 使用方式：app iot <子命令>
 */
#define IO_TEST_ENABLE 1

#if IO_TEST_ENABLE

/********************************************************************
**函数名称:  iot_hex_val
**入口参数:  c       ---        待解析字符
**出口参数:  无
**函数功能:  将单个十六进制字符转换为数值
**返回值:    0~15 表示有效，-1 表示非法字符
*********************************************************************/
static int iot_hex_val(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        return c - 'a' + 10;
    }

    if ((c >= 'A') && (c <= 'F'))
    {
        return c - 'A' + 10;
    }

    return -1;
}

/********************************************************************
**函数名称:  iot_parse_hex
**入口参数:  s        ---        待解析字符串（输入）
            buf      ---        输出缓冲区（输入）
            max_len  ---        缓冲区最大长度（输入）
**出口参数:  buf      ---        解析出的十六进制字节（输出）
**函数功能:  解析 "AA BB CC" 或 "AABBCC" 或带 0x 前缀的十六进制字符串
**返回值:    解析得到的字节数，失败返回 0
*********************************************************************/
static int iot_parse_hex(const char *s, uint8_t *buf, int max_len)
{
    int len;
    int i;
    int hi;
    int lo;

    len = 0;
    i = 0;

    while ((s[i] != '\0') && (len < max_len))
    {
        /* 跳过空白和逗号分隔符 */
        while ((s[i] == ' ') || (s[i] == ','))
        {
            i++;
        }

        if (s[i] == '\0')
        {
            break;
        }

        /* 跳过 0x 前缀 */
        if ((s[i] == '0') && ((s[i + 1] == 'x') || (s[i + 1] == 'X')))
        {
            i += 2;
        }

        hi = iot_hex_val(s[i]);
        if (hi < 0)
        {
            break;
        }
        i++;

        lo = iot_hex_val(s[i]);
        if (lo < 0)
        {
            break;
        }
        i++;

        buf[len] = (uint8_t)((hi << 4) | lo);
        len++;
    }

    return len;
}

/********************************************************************
**函数名称:  iot_parse_on_off
**入口参数:  s       ---        待解析字符串（输入）
**出口参数:  无
**函数功能:  解析 on/off 或 0/1 开关参数
**返回值:    1 表示开，0 表示关，-1 表示非法
*********************************************************************/
static int iot_parse_on_off(const char *s)
{
    if ((strcmp(s, "on") == 0) || (strcmp(s, "1") == 0))
    {
        return 1;
    }

    if ((strcmp(s, "off") == 0) || (strcmp(s, "0") == 0))
    {
        return 0;
    }

    return -1;
}

/********************************************************************
**函数名称:  cmd_iot_pwr
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  测试各模块电源控制 GPIO（4G/充电/WIFI/气压计/六轴/LCD）
**返回值:    无
**示例:      app iot pwr gsensor on
*********************************************************************/
static void cmd_iot_pwr(const struct shell *sh, size_t argc, char **argv)
{
    static const struct gpio_dt_spec lte_pwr_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(lte_pwr_ctrl), gpios);
    int on;
    int ret;

    if (argc < 3)
    {
        shell_error(sh, "Usage: iot pwr <4g|charge|wifi|baro|gsensor|lcd> <on|off>");
        return;
    }

    on = iot_parse_on_off(argv[2]);
    if (on < 0)
    {
        shell_error(sh, "Invalid arg: %s (expect on/off or 0/1)", argv[2]);
        return;
    }

    if (strcmp(argv[1], "4g") == 0)
    {
        /* 4G电源：P2.06，高电平有效 */
        ret = gpio_pin_configure_dt(&lte_pwr_gpio, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
    }
    else if (strcmp(argv[1], "charge") == 0)
    {
        /* 充电使能：P2.07，低电平使能 */
        charge_enable(on);
        ret = 0;
    }
    else if (strcmp(argv[1], "wifi") == 0)
    {
        /* WIFI电源：P2.08，高电平有效 */
        ret = my_wifi_pwr_on(on);
    }
    else if (strcmp(argv[1], "baro") == 0)
    {
        /* 气压计电源：P2.09，高电平有效 */
        ret = barometer_pwr_on(on);
    }
    else if (strcmp(argv[1], "gsensor") == 0)
    {
        /* 六轴电源：P2.10，低电平有效 */
        ret = my_gsensor_pwr_on(on);
    }
    else if (strcmp(argv[1], "lcd") == 0)
    {
        /* LCD电源：P2.00，高电平有效 */
        ret = my_lcd_pwr_on(on);
    }
    else
    {
        shell_error(sh, "Invalid target: %s", argv[1]);
        return;
    }

    if (ret != 0)
    {
        shell_error(sh, "iot pwr %s %d fail: %d", argv[1], on, ret);
        return;
    }

    shell_print(sh, "iot pwr %s = %s OK", argv[1], on ? "ON" : "OFF");
}

/********************************************************************
**函数名称:  cmd_iot_i2cscan
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  扫描指定 I2C 总线上的从机地址
**返回值:    无
**示例:      app iot i2cscan 21
*********************************************************************/
static void cmd_iot_i2cscan(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev;
    struct i2c_msg msg;
    uint8_t addr;
    uint8_t tmp;
    int found;

    if (argc < 2)
    {
        shell_error(sh, "Usage: iot i2cscan <21|22>");
        return;
    }

    if (strcmp(argv[1], "21") == 0)
    {
        /* i2c21：六轴+库仑计共用总线 */
        dev = DEVICE_DT_GET(DT_ALIAS(gsensor_i2c));
    }
    else if (strcmp(argv[1], "22") == 0)
    {
        /* i2c22：气压计+温湿度共用总线 */
        dev = DEVICE_DT_GET(DT_ALIAS(baro_i2c));
    }
    else
    {
        shell_error(sh, "Invalid bus: %s (only 21/22)", argv[1]);
        return;
    }

    if (!device_is_ready(dev))
    {
        shell_error(sh, "I2C bus %s not ready", argv[1]);
        return;
    }

    found = 0;
    shell_print(sh, "I2C scan on bus %s ...", argv[1]);

    for (addr = 8; addr <= 0x77; addr++)
    {
        tmp = 0;
        msg.buf = &tmp;
        msg.len = 1;
        msg.flags = I2C_MSG_STOP;

        /* 以 1 字节写探测从机是否应答 */
        if (i2c_transfer(dev, &msg, 1, addr) == 0)
        {
            shell_print(sh, "  found device at 0x%02X", addr);
            found++;
        }
    }

    shell_print(sh, "Scan done, %d device(s) found", found);
}

/********************************************************************
**函数名称:  cmd_iot_gsensor
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  读取 QMI8658B 六轴数据
**返回值:    无
**示例:      app iot gsensor
*********************************************************************/
static void cmd_iot_gsensor(const struct shell *sh, size_t argc, char **argv)
{
    my_gsensor_data_t data;
    int ret;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    ret = my_gsensor_read_data(&data);
    if (ret != 0)
    {
        shell_error(sh, "QMI8658B read fail: %d", ret);
        return;
    }

    shell_print(sh, "ACC(mg): %d,%d,%d  GYR(mdps): %d,%d,%d",
                data.acc_x_mg, data.acc_y_mg, data.acc_z_mg,
                data.gyr_x_mdps, data.gyr_y_mdps, data.gyr_z_mdps);
}

/********************************************************************
**函数名称:  cmd_iot_gauge
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  读取 OM70201WV 库仑计电量数据
**返回值:    无
**示例:      app iot gauge
*********************************************************************/
static void cmd_iot_gauge(const struct shell *sh, size_t argc, char **argv)
{
    battery_gauge_data_t data;
    battery_gauge_result_t ret;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    ret = battery_gauge_read(&data);
    if (ret != BATTERY_GAUGE_SUCCESS)
    {
        shell_error(sh, "OM70201WV read fail: %d", ret);
        return;
    }

    shell_print(sh, "Vbat=%umV Ibat=%dmA Tbat=%dC SOC=%u%% SOH=%u%%",
                data.voltage_mv, data.current_ma, data.temperature_c,
                data.soc_percent, data.soh_percent);
}

/********************************************************************
**函数名称:  cmd_iot_baro
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  读取 SPA06 气压计数据
**返回值:    无
**示例:      app iot baro
*********************************************************************/
static void cmd_iot_baro(const struct shell *sh, size_t argc, char **argv)
{
    struct barometer_data data;
    barometer_result_t ret;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    ret = barometer_read(&data);
    if (ret != BARO_SUCCESS)
    {
        shell_error(sh, "SPA06 read fail: %d", ret);
        return;
    }

    shell_print(sh, "Pressure=%dPa Temp=%d.%02dC",
                data.pressure_pa, data.temperature / 100, data.temperature % 100);
}

/********************************************************************
**函数名称:  cmd_iot_charge
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  读取充电检测引脚（P0.03）电平
**返回值:    无
**示例:      app iot charge
*********************************************************************/
static void cmd_iot_charge(const struct shell *sh, size_t argc, char **argv)
{
    int level;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    level = get_charge_state_level();
    shell_print(sh, "Charge detect level = %d (%s)", level,
                level == 1 ? "CHARGING" : "NO_CHARGE");
}

/********************************************************************
**函数名称:  cmd_iot_key
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  读取功能按键（P1.09）与 SOS 按键（P1.14）电平
**返回值:    无
**示例:      app iot key
*********************************************************************/
static void cmd_iot_key(const struct shell *sh, size_t argc, char **argv)
{
    static const struct gpio_dt_spec fun_key_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(fun_key), gpios);
    static const struct gpio_dt_spec sos_key_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(sos_key), gpios);
    int fun_level;
    int sos_level;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    fun_level = gpio_pin_get_dt(&fun_key_gpio);
    sos_level = gpio_pin_get_dt(&sos_key_gpio);

    shell_print(sh, "FUN_KEY(P1.09)=%d  SOS_KEY(P1.14)=%d",
                fun_level, sos_level);
}

/********************************************************************
**函数名称:  cmd_iot_lcd
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  控制 LCD 屏电源开关
**返回值:    无
**示例:      app iot lcd on
*********************************************************************/
static void cmd_iot_lcd(const struct shell *sh, size_t argc, char **argv)
{
    int on;
    int ret;

    if (argc < 2)
    {
        shell_error(sh, "Usage: iot lcd <on|off>");
        return;
    }

    on = iot_parse_on_off(argv[1]);
    if (on < 0)
    {
        shell_error(sh, "Invalid arg: %s", argv[1]);
        return;
    }

    ret = my_lcd_pwr_on(on);
    if (ret != 0)
    {
        shell_error(sh, "LCD pwr %s fail: %d", argv[1], ret);
        return;
    }

    shell_print(sh, "LCD power = %s OK", on ? "ON" : "OFF");
}

/********************************************************************
**函数名称:  cmd_iot_lcdbl
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  调节 LCD 背光亮度（0~100）
**返回值:    无
**示例:      app iot lcdbl 80
*********************************************************************/
static void cmd_iot_lcdbl(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t level;
    int ret;

    if (argc < 2)
    {
        shell_error(sh, "Usage: iot lcdbl <0-100>");
        return;
    }

    level = (uint8_t)strtoul(argv[1], NULL, 10);
    if (level > 100)
    {
        shell_error(sh, "Invalid level: %s (0-100)", argv[1]);
        return;
    }

    ret = my_lcd_set_backlight(level);
    if (ret != 0)
    {
        shell_error(sh, "LCD backlight set fail: %d", ret);
        return;
    }

    shell_print(sh, "LCD backlight = %d%% OK", level);
}

/********************************************************************
**函数名称:  cmd_iot_lcdclear
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  以指定 RGB565 颜色清屏
**返回值:    无
**示例:      app iot lcdclear F800   (红色)
*********************************************************************/
static void cmd_iot_lcdclear(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t buf[2];
    uint16_t color;
    int len;
    int ret;

    if (argc < 2)
    {
        shell_error(sh, "Usage: iot lcdclear <hex16>  e.g. F800=red 07E0=green 001F=blue");
        return;
    }

    len = iot_parse_hex(argv[1], buf, sizeof(buf));
    if (len != 2)
    {
        shell_error(sh, "Invalid color: %s", argv[1]);
        return;
    }

    color = (uint16_t)((buf[0] << 8) | buf[1]);

    ret = my_lcd_clear(color);
    if (ret != 0)
    {
        shell_error(sh, "LCD clear fail: %d", ret);
        return;
    }

    shell_print(sh, "LCD clear 0x%04X OK", color);
}

/********************************************************************
**函数名称:  cmd_iot_buzzer
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  播放一段蜂鸣器提示音
**返回值:    无
**示例:      app iot buzzer
*********************************************************************/
static void cmd_iot_buzzer(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    my_ctrl_buzzer_play_tone(2000, 300);
    shell_print(sh, "Buzzer 2KHz 300ms played");
}

/********************************************************************
**函数名称:  cmd_iot_batt
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  读取电池 ADC 电压
**返回值:    无
**示例:      app iot batt
*********************************************************************/
static void cmd_iot_batt(const struct shell *sh, size_t argc, char **argv)
{
    int32_t mv;
    int ret;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    ret = my_battery_read_mv(&mv);
    if (ret != 0)
    {
        shell_error(sh, "Battery ADC read fail: %d", ret);
        return;
    }

    shell_print(sh, "Battery voltage = %dmV", mv);
}

/********************************************************************
**函数名称:  cmd_iot_ltetx
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  通过 LTE 串口发送十六进制数据
**返回值:    无
**示例:      app iot ltetx "AA 0D 0A"
*********************************************************************/
static void cmd_iot_ltetx(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t buf[64];
    int len;
    int ret;

    if (argc < 2)
    {
        shell_error(sh, "Usage: iot ltetx <hexstr>  e.g. \"AA 0D 0A\"");
        return;
    }

    len = iot_parse_hex(argv[1], buf, sizeof(buf));
    if (len <= 0)
    {
        shell_error(sh, "No valid hex data");
        return;
    }

    ret = my_lte_uart_send(buf, (uint16_t)len);
    if (ret != 0)
    {
        shell_error(sh, "LTE UART send fail: %d", ret);
        return;
    }

    shell_print(sh, "LTE TX %d bytes OK", len);
}

/********************************************************************
**函数名称:  cmd_iot_wifitx
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  通过 WIFI 串口发送十六进制数据
**返回值:    无
**示例:      app iot wifitx "PING"
*********************************************************************/
static void cmd_iot_wifitx(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t buf[64];
    int len;
    int ret;

    if (argc < 2)
    {
        shell_error(sh, "Usage: iot wifitx <hexstr>  e.g. \"PING\"");
        return;
    }

    len = iot_parse_hex(argv[1], buf, sizeof(buf));
    if (len <= 0)
    {
        shell_error(sh, "No valid hex data");
        return;
    }

    ret = my_wifi_uart_send(buf, (uint16_t)len);
    if (ret != 0)
    {
        shell_error(sh, "WIFI UART send fail: %d", ret);
        return;
    }

    shell_print(sh, "WIFI TX %d bytes OK", len);
}

/********************************************************************
**函数名称:  cmd_iot_wake
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  读取 LTE/WIFI 唤醒引脚电平
**返回值:    无
**示例:      app iot wake lte
*********************************************************************/
static void cmd_iot_wake(const struct shell *sh, size_t argc, char **argv)
{
    static const struct gpio_dt_spec lte_wake_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(lte_wake_ctrl), gpios);
    static const struct gpio_dt_spec wifi_wake_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(wifi_wake_ctrl), gpios);
    int level;

    if (argc < 2)
    {
        shell_error(sh, "Usage: iot wake <lte|wifi>");
        return;
    }

    if (strcmp(argv[1], "lte") == 0)
    {
        level = gpio_pin_get_dt(&lte_wake_gpio);
    }
    else if (strcmp(argv[1], "wifi") == 0)
    {
        level = gpio_pin_get_dt(&wifi_wake_gpio);
    }
    else
    {
        shell_error(sh, "Invalid target: %s", argv[1]);
        return;
    }

    shell_print(sh, "%s wake pin level = %d", argv[1], level);
}

/********************************************************************
**函数名称:  cmd_iot_all
**入口参数:  sh       ---        shell 句柄（输入）
            argc     ---        参数个数（输入）
            argv     ---        参数数组（输入）
**出口参数:  无
**函数功能:  依次执行关键 IO 外设自检
**返回值:    无
**示例:      app iot all
*********************************************************************/
static void cmd_iot_all(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== IO test start ===");
    cmd_iot_charge(sh, 0, NULL);
    cmd_iot_key(sh, 0, NULL);
    cmd_iot_batt(sh, 0, NULL);
    cmd_iot_gsensor(sh, 0, NULL);
    cmd_iot_gauge(sh, 0, NULL);
    cmd_iot_baro(sh, 0, NULL);
    cmd_iot_buzzer(sh, 0, NULL);
    shell_print(sh, "=== IO test done ===");
}

/* IO 测试子命令注册表 */
SHELL_STATIC_SUBCMD_SET_CREATE(iot_sub,
    SHELL_CMD(pwr, NULL, "iot pwr <4g|charge|wifi|baro|gsensor|lcd> <on|off>", cmd_iot_pwr),
    SHELL_CMD(i2cscan, NULL, "iot i2cscan <21|22>", cmd_iot_i2cscan),
    SHELL_CMD(gsensor, NULL, "Read QMI8658B 6-axis data", cmd_iot_gsensor),
    SHELL_CMD(gauge, NULL, "Read OM70201WV gauge data", cmd_iot_gauge),
    SHELL_CMD(baro, NULL, "Read SPA06 barometer data", cmd_iot_baro),
    SHELL_CMD(charge, NULL, "Read charge detect pin", cmd_iot_charge),
    SHELL_CMD(key, NULL, "Read FUN/SOS key level", cmd_iot_key),
    SHELL_CMD(lcd, NULL, "iot lcd <on|off>", cmd_iot_lcd),
    SHELL_CMD(lcdbl, NULL, "iot lcdbl <0-100>", cmd_iot_lcdbl),
    SHELL_CMD(lcdclear, NULL, "iot lcdclear <hex16>", cmd_iot_lcdclear),
    SHELL_CMD(buzzer, NULL, "Play buzzer tone", cmd_iot_buzzer),
    SHELL_CMD(batt, NULL, "Read battery ADC voltage", cmd_iot_batt),
    SHELL_CMD(ltetx, NULL, "iot ltetx <hexstr>", cmd_iot_ltetx),
    SHELL_CMD(wifitx, NULL, "iot wifitx <hexstr>", cmd_iot_wifitx),
    SHELL_CMD(wake, NULL, "iot wake <lte|wifi>", cmd_iot_wake),
    SHELL_CMD(all, NULL, "Run all IO self-test", cmd_iot_all),
    SHELL_SUBCMD_SET_END
);

#endif /* IO_TEST_ENABLE */

/* 注册自定义命令到 Shell 子系统 */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_app,
    SHELL_CMD(sysinfo, NULL, "Display system information", cmd_system_info),
    SHELL_CMD(bleinfo, NULL, "Display BLE status", cmd_ble_info),
    SHELL_CMD(reboot, NULL, "Reboot system", cmd_reboot),
    SHELL_CMD(AT_TEST, NULL, "Usage:app AT_TEST \"TEST xxxx(AT^GT_CM=xxxx)\"", shell_at_test),
    SHELL_CMD(shutdown, NULL, "Shutdown system (enter ultra-low power mode)", cmd_shutdown),
    SHELL_CMD(blog, NULL, "Send BLE log test message: app blog <message>", cmd_ble_log_test),
    SHELL_CMD(blogcfg, NULL, "BLE log config: app blogcfg <global|mod|level|show>", cmd_ble_log_config),
    SHELL_CMD(buzzer_test, NULL, "Run Buzzer test", cmd_buzzer_test),
    SHELL_CMD(retransmit_check_test, NULL, "Run retransmit_check_test test", cmd_retransmit_check_test),
    SHELL_CMD(hardware_test, NULL, "Run hardware test", cmd_hardware_test),
    SHELL_CMD(read_gsensor_data, NULL, "Read G-Sensor data", cmd_read_gsensor_data),
#if IO_TEST_ENABLE
    SHELL_CMD(iot, &iot_sub, "IO peripheral test commands", NULL),
#endif
    SHELL_SUBCMD_SET_END
);
/* Zephyr Shell 子系统提供的宏，随 nRF Connect SDK一起提供，用来在 Shell里注册一个“根命令”
 * 这个宏在头文件zephyr/shell/shell.h里定义，是Zephyr的Shell API的一部分
 */
SHELL_CMD_REGISTER(app, &sub_app, "Application commands", NULL);

/********************************************************************
**函数名称:  my_shell_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化 Shell 模块（Zephyr Shell 自动初始化，此处仅做日志输出）
**返 回 值:  0 表示成功
*********************************************************************/
int my_shell_init(void)
{
    LOG_INF("Shell module initialized (RTT backend)");
    LOG_INF("Use 'app sysinfo', 'app bleinfo', etc. to interact");
    return 0;
}
