/**
 * 4x4 矩阵键盘驱动
 *
 * 硬件连接：
 *   行 (输出): PB0 ~ PB3  （我们控制哪一行拉低）
 *   列 (输入): PB4 ~ PB7  （我们读取哪一列变低了）
 *
 * 扫描原理：
 *   1. 所有行输出高电平
 *   2. 某一行输出低电平（扫描这一行）
 *   3. 读列的状态，如果有列变低 → 那个位置的键被按下了
 *   4. 四行轮流扫描，就能检测 16 个键
 *
 * 这个文件你需要理解的核心就一件事：
 *   怎么用 GPIO 做"扫描"—— 轮流拉低一行，然后读列。
 */

#include "keypad.h"
#include "delay.h"

/* ========== 引脚定义（根据你的实际接线修改）========== */

/* 行引脚 —— PB0 ~ PB3，配置为输出 */
#define ROW_PORT        GPIOB
#define ROW0_PIN        GPIO_Pin_0
#define ROW1_PIN        GPIO_Pin_1
#define ROW2_PIN        GPIO_Pin_2
#define ROW3_PIN        GPIO_Pin_3
#define ROW_ALL_PIN     (ROW0_PIN | ROW1_PIN | ROW2_PIN | ROW3_PIN)
#define ROW_RCC         RCC_AHB1Periph_GPIOB

/* 列引脚 —— PB4 ~ PB7，配置为输入（内部上拉） */
#define COL_PORT        GPIOB
#define COL0_PIN        GPIO_Pin_4
#define COL1_PIN        GPIO_Pin_5
#define COL2_PIN        GPIO_Pin_6
#define COL3_PIN        GPIO_Pin_7
#define COL_ALL_PIN     (COL0_PIN | COL1_PIN | COL2_PIN | COL3_PIN)
#define COL_RCC         RCC_AHB1Periph_GPIOB

/* 四行引脚数组，方便循环扫描 */
static const uint16_t rowPins[4] = {ROW0_PIN, ROW1_PIN, ROW2_PIN, ROW3_PIN};

/* 键值映射表：keyMap[行][列] = 对应的字符
 * 行号 0~3，列号 0~3，对应 16 个键 */
static const char keyMap[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

/* ========== 初始化函数 ========== */

/**
 * KeyPad_Init - 初始化矩阵键盘的 GPIO
 *
 * 做两件事：
 *   1. 行引脚(PB0~PB3) 配置为推挽输出，默认高电平
 *   2. 列引脚(PB4~PB7) 配置为输入，内部上拉
 *
 * 为什么列要上拉？
 *   因为按键没按下时，我们需要读到高电平。
 *   内部上拉电阻就是干这个的 —— 没人拉低它，它就是高。
 */
void KeyPad_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* 开启 GPIOB 的时钟
     * STM32 的外设都要先开时钟才能用，这是低功耗设计 */
    RCC_AHB1PeriphClockCmd(ROW_RCC | COL_RCC, ENABLE);

    /* ---------- 配置行引脚：推挽输出，默认高电平 ---------- */
    GPIO_InitStruct.GPIO_Pin   = ROW_ALL_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;     /* 输出模式 */
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;      /* 推挽输出（能主动输出高和低） */
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;   /* 翻转速度 */
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;   /* 不需要上下拉，因为我们主动控制 */
    GPIO_Init(ROW_PORT, &GPIO_InitStruct);

    /* 默认把所有行拉高（不扫描任何行） */
    GPIO_SetBits(ROW_PORT, ROW_ALL_PIN);

    /* ---------- 配置列引脚：输入，内部上拉 ---------- */
    GPIO_InitStruct.GPIO_Pin   = COL_ALL_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IN;      /* 输入模式 */
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_UP;       /* 内部上拉！没按键时读到高电平 */
    GPIO_Init(COL_PORT, &GPIO_InitStruct);
}

/* ========== 核心扫描函数 ========== */

/**
 * KeyPad_Scan - 扫描键盘，返回按下的键的字符
 *
 * 返回值：
 *   '0'~'9', 'A'~'D', '*', '#'  —— 有键按下
 *   '\0' (0)                      —— 没有键按下
 *
 * 扫描过程（以扫描第 0 行为例）：
 *   1. 把 PB0 拉低，其他行(PB1~PB3)保持高电平
 *   2. 读 PB4~PB7 的状态
 *   3. 如果 PB4 变低了 → 第0行第0列的键( '1' )被按下了
 *   4. 如果 PB5 变低了 → 第0行第1列的键( '2' )被按下了
 *   5. ...以此类推
 *   6. 然后扫描第 1 行、第 2 行、第 3 行
 *
 * 一个需要注意的细节：
 *   按键会有"抖动"（按下瞬间信号会跳几下），
 *   所以检测到按键后要延时 10~20ms 再确认一次。
 *   这叫做"消抖"。
 */
char KeyPad_Scan(void)
{
    uint8_t row, col;
    uint16_t colState;

    for (row = 0; row < 4; row++) {
        /* 第 1 步：所有行拉高 */
        GPIO_SetBits(ROW_PORT, ROW_ALL_PIN);

        /* 第 2 步：只把当前扫描的这一行拉低 */
        GPIO_ResetBits(ROW_PORT, rowPins[row]);

        /* 第 3 步：短暂延时，等 GPIO 电平稳定
         * （GPIO 切换后需要几微秒才能稳定） */
        Delay_us(10);

        /* 第 4 步：读列的状态
         * GPIO_ReadInputData() 返回整个端口的 16 位数据
         * 我们只关心 PB4~PB7 这 4 位 */
        colState = GPIO_ReadInputData(COL_PORT);

        /* 第 5 步：检查每一列
         * 如果某一列为低电平（说明该位置的键被按下了） */
        for (col = 0; col < 4; col++) {
            /* GPIO_Pin_4 = 0x0010, GPIO_Pin_5 = 0x0020, ...
             * 用位与操作检查该引脚是否为低电平 */
            uint16_t colPin = COL0_PIN << col;  /* 第 col 列对应的引脚 */

            if (!(colState & colPin)) {
                /* 检测到低电平 → 有键按下 → 消抖 */
                Delay_ms(15);

                /* 再读一次确认（消抖） */
                colState = GPIO_ReadInputData(COL_PORT);
                if (!(colState & colPin)) {
                    /* 等待按键松开（阻塞式）
                     * 生产代码可以用非阻塞方式，但学习阶段先用简单的 */
                    while (!(GPIO_ReadInputData(COL_PORT) & colPin));

                    /* 恢复所有行高电平 */
                    GPIO_SetBits(ROW_PORT, ROW_ALL_PIN);

                    /* 返回对应的键值 */
                    return keyMap[row][col];
                }
            }
        }
    }

    /* 扫描完四行都没检测到按键 */
    GPIO_SetBits(ROW_PORT, ROW_ALL_PIN);
    return '\0';  /* 没有键按下 */
}

/**
 * KeyPad_ScanNonBlocking - 非阻塞扫描（高级用法，先不看这个）
 *
 * 和 KeyPad_Scan 的区别：
 *   - Scan 会阻塞等待按键松开
 *   - ScanNonBlocking 只检测按下瞬间，不等待松开
 *
 * 这个适合用在主循环里，和其他任务交替执行。
 * 初学阶段先用 KeyPad_Scan，等你熟悉了再回来看这个。
 */
char KeyPad_ScanNonBlocking(void)
{
    static uint8_t lastKey = 0;  /* 上次扫描到的键，用于去重 */
    uint8_t row, col;
    uint16_t colState;

    for (row = 0; row < 4; row++) {
        GPIO_SetBits(ROW_PORT, ROW_ALL_PIN);
        GPIO_ResetBits(ROW_PORT, rowPins[row]);
        Delay_us(10);

        colState = GPIO_ReadInputData(COL_PORT);

        for (col = 0; col < 4; col++) {
            uint16_t colPin = COL0_PIN << col;

            if (!(colState & colPin)) {
                Delay_ms(15);
                colState = GPIO_ReadInputData(COL_PORT);
                if (!(colState & colPin)) {
                    char key = keyMap[row][col];
                    if (key != lastKey) {
                        lastKey = key;
                        GPIO_SetBits(ROW_PORT, ROW_ALL_PIN);
                        return key;
                    }
                }
            }
        }
    }

    lastKey = 0;  /* 没有键按下，清除上次记录 */
    GPIO_SetBits(ROW_PORT, ROW_ALL_PIN);
    return '\0';
}
