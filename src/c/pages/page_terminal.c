#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "hal_oled.h"
#include "page_interface.h"
#include "menu.h"
#include "font.h"

#define OLED_WIDTH    128
#define CHAR_WIDTH    6
#define MAX_COLS      21   // 128/6 ≈ 21字符
#define CMD_BUF_SIZE  64

// 终端状态
typedef struct {
    char input_buf[CMD_BUF_SIZE];      // 当前输入命令
    int input_len;                     // 输入长度
    int cursor_pos;                    // 光标位置（暂不支持，输入总在末尾）
    
    char history[4][MAX_COLS+1];       // 4行历史输出
    int history_line;                  // 当前历史行指针(0-3循环)
    int history_count;                 // 历史行数（用于显示）
    
    int quick_cmd_idx;                 // 快速命令索引
} terminal_state_t;

static terminal_state_t term = {0};
static int terminal_active = 0;

// 预定义快捷命令（通过UP/DOWN选择）
static const char* quick_cmds[] = {
    "ifconfig",
    "top -n 1",
    "ls -la",
    "ps aux",
    "cat /proc/cpuinfo",
    "clear",
    "reboot",
    "poweroff"
};
#define QUICK_CMD_COUNT (sizeof(quick_cmds)/sizeof(quick_cmds[0]))
#define QUICK_CMD_MAX   8

// 内部函数声明
static void terminal_execute(const char* cmd);
static void terminal_clear_history(void);
static void terminal_add_output(const char* text);
static void terminal_update_input_display(void);
static void terminal_insert_char(char c);
static void terminal_backspace(void);

// 绘制终端页面
static void page_terminal_draw(void)
{
    char buf[65];
    int i, idx, y;
    
    hal_oled_clear();
    
    // 标题栏
    hal_oled_string(0, 0, ">$ Terminal");
    hal_oled_line(0, 10, 127, 10);
    
    // 显示历史输出（4行，y=12开始，每行10像素高度）
    y = 12;
    for (i = 0; i < 4; i++) {
        // 计算要显示的行（最新的在底部）
        idx = (term.history_line + i) % 4;
        if (term.history[idx][0] != '\0') {
            hal_oled_string(0, y, term.history[idx]);
        }
        y += 10;
    }
    
    // 显示输入行（y=52，留出空间给光标）
    if (term.input_len > 0) {
        // 只显示能 fit 屏幕的部分
        int start = 0;
        if (term.input_len > MAX_COLS - 2) {
            start = term.input_len - (MAX_COLS - 2);
        }
        snprintf(buf, sizeof(buf), ">%s", &term.input_buf[start]);
    } else {
        buf[0] = '>';
        buf[1] = '\0';
    }
    hal_oled_string(0, 52, buf);
    
    hal_oled_refresh();
}

// 处理页面事件（兼容现有事件系统：EV_UP/DOWN/ENTER/BACK）
static void page_terminal_handle_event(event_t ev)
{
    switch (ev) {
        case EV_UP:
            // 快速命令上翻
            if (term.quick_cmd_idx > 0) {
                term.quick_cmd_idx--;
            } else {
                term.quick_cmd_idx = QUICK_CMD_COUNT - 1;
            }
            strncpy(term.input_buf, quick_cmds[term.quick_cmd_idx], CMD_BUF_SIZE-1);
            term.input_buf[CMD_BUF_SIZE-1] = '\0';
            term.input_len = strlen(term.input_buf);
            page_terminal_draw();
            break;
            
        case EV_DOWN:
            // 快速命令下翻
            term.quick_cmd_idx++;
            if (term.quick_cmd_idx >= QUICK_CMD_COUNT) {
                term.quick_cmd_idx = 0;
            }
            strncpy(term.input_buf, quick_cmds[term.quick_cmd_idx], CMD_BUF_SIZE-1);
            term.input_buf[CMD_BUF_SIZE-1] = '\0';
            term.input_len = strlen(term.input_buf);
            page_terminal_draw();
            break;
            
        case EV_ENTER:
            // 执行命令
            if (term.input_len > 0) {
                terminal_execute(term.input_buf);
                term.input_buf[0] = '\0';
                term.input_len = 0;
                term.cursor_pos = 0;
                page_terminal_draw();
            }
            break;
            
        case EV_BACK:
            // 返回菜单
            terminal_active = 0;
            extern void menu_back(void);
            menu_back();
            break;
            
        default:
            break;
    }
}

// 添加字符到输入（被外部调用，比如USB键盘驱动）
void terminal_input_char(char c)
{
    if (!terminal_active) return;
    
    if (c == '\n' || c == '\r') {
        page_terminal_handle_event(EV_ENTER);
    } else if (c == 127 || c == '\b' || c == 8) {
        // Backspace (DEL)
        terminal_backspace();
        page_terminal_draw();
    } else if (c >= 32 && c < 127 && term.input_len < CMD_BUF_SIZE - 1) {
        // 可打印ASCII
        term.input_buf[term.input_len] = c;
        term.input_len++;
        term.input_buf[term.input_len] = '\0';
        page_terminal_draw();
    }
}

// 退格处理
static void terminal_backspace(void)
{
    if (term.input_len > 0) {
        term.input_len--;
        term.input_buf[term.input_len] = '\0';
    }
}

// 执行命令
static void terminal_execute(const char* cmd)
{
    char output_line[MAX_COLS+1];
    FILE* fp;
    int lines = 0;
    
    // 先显示执行的命令到历史
    snprintf(output_line, sizeof(output_line), ">%s", cmd);
    terminal_add_output(output_line);
    
    // 处理内部命令
    if (strcmp(cmd, "clear") == 0) {
        terminal_clear_history();
        return;
    } else if (strcmp(cmd, "exit") == 0) {
        page_terminal_handle_event(EV_BACK);
        return;
    } else if (strcmp(cmd, "reboot") == 0) {
        terminal_add_output("Rebooting...");
        hal_oled_refresh();
        sleep(1);
        system("reboot");
        return;
    } else if (strcmp(cmd, "poweroff") == 0) {
        terminal_add_output("Shutting down...");
        hal_oled_refresh();
        sleep(1);
        system("poweroff");
        return;
    }
    
    // 执行系统命令
    fp = popen(cmd, "r");
    if (fp == NULL) {
        terminal_add_output("Exec failed");
        return;
    }
    
    // 读取最多4行输出显示
    while (fgets(output_line, sizeof(output_line), fp) != NULL && lines < 4) {
        // 去掉换行符
        output_line[strcspn(output_line, "\n")] = '\0';
        // 截断到屏幕宽度
        if (strlen(output_line) > MAX_COLS) {
            output_line[MAX_COLS] = '\0';
        }
        terminal_add_output(output_line);
        lines++;
    }
    
    pclose(fp);
    
    // 如果还有更多输出，提示省略
    if (lines >= 4 && !feof(fp)) {
        terminal_add_output("...(more)");
    }
}

// 添加输出行到历史（滚动显示）
static void terminal_add_output(const char* text)
{
    strncpy(term.history[term.history_line], text, MAX_COLS);
    term.history[term.history_line][MAX_COLS] = '\0';
    term.history_line = (term.history_line + 1) % 4;
    if (term.history_count < 4) {
        term.history_count++;
    }
}

// 清空历史
static void terminal_clear_history(void)
{
    memset(term.history, 0, sizeof(term.history));
    term.history_line = 0;
    term.history_count = 0;
    page_terminal_draw();
}

// 初始化终端
void page_terminal_init(void)
{
    static int initialized = 0;
    if (initialized) return;
    
    page_register("Terminal", page_terminal_draw, page_terminal_handle_event);
    
    // 初始化状态
    memset(&term, 0, sizeof(term));
    term.quick_cmd_idx = 0;
    terminal_active = 0;
    
    initialized = 1;
}

// 创建菜单项（供主菜单调用）
menu_item_t* page_terminal_create_menu(void)
{
    page_terminal_init();
    
    // 图标可以使用 tools 的图标或自定义
    extern const unsigned char icon_tools_28x28[];  // 临时借用tools图标，或创建新的
    menu_item_t* terminal_node = menu_create("Terminal", page_terminal_draw, icon_terminal_28x28);
    
    terminal_active = 1;
    
    return terminal_node;
}