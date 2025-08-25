#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h> // 为了使用 PRId64 宏


#include <pthread.h>
#include <sys/types.h>
#include <netdb.h>
// libuv 头文件
#include <uv.h>

// cJSON 头文件
#include <cjson/cJSON.h>

// 定义一个结构体来传递数据给回调函数
typedef struct {
    uv_fs_t open_req;
    uv_fs_t read_req;
    uv_file file;
    char buffer[4096];
    size_t buffer_size;
    const char* target_mode;
} file_read_ctx_t;

// 定义一个结构体来跟踪子进程
typedef struct {
    uv_process_t process;
    uv_process_options_t options;
    char* args[3]; // e.g., {"/path/to/app", NULL}
} process_ctx_t;

// 全局变量，用于跟踪正在运行的进程数量
int active_processes = 0;

// 函数声明
void on_read_complete(uv_fs_t* req);
void on_process_exit(uv_process_t* req, int64_t exit_status, int term_signal);
void launch_apps_for_mode(cJSON* config, const char* mode_name);

// --- 主函数 ---
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mode_name>\n", argv[0]);
        fprintf(stderr, "Available modes are defined in config.json\n");
        return 1;
    }

    // 初始化 libuv 默认事件循环
    uv_loop_t* loop = uv_default_loop();

    // 分配上下文内存
    file_read_ctx_t* context = (file_read_ctx_t*)malloc(sizeof(file_read_ctx_t));
    if (!context) {
        perror("Failed to allocate memory for context");
        return 1;
    }
    memset(context, 0, sizeof(file_read_ctx_t));
    context->target_mode = argv[1];

    // 1. 异步打开文件
    const char* filename = "config.json";
    uv_fs_open(loop, &context->open_req, filename, O_RDONLY, 0, NULL);

    // --- 修正点 2: 准备 uv_buf_t 用于读取 ---
    uv_buf_t iov = uv_buf_init(context->buffer, sizeof(context->buffer) - 1);

    // 2. 设置读取完成后的回调
    context->read_req.data = context;
    // 将 char* buffer 替换为 uv_buf_t* iov
    uv_fs_read(loop, &context->read_req, context->open_req.result, &iov, 1, -1, on_read_complete);

    // 3. 运行事件循环，直到所有任务完成
    uv_run(loop, UV_RUN_DEFAULT);

    // 清理资源
    uv_fs_req_cleanup(&context->open_req);
    uv_fs_req_cleanup(&context->read_req);
    free(context);
    
    // 检查是否所有进程都已启动并退出
    if (active_processes > 0) {
        printf("Waiting for launched applications to close...\n");
        uv_run(loop, UV_RUN_DEFAULT); // 再次运行循环，等待进程退出
    }

    printf("All tasks complete. Launcher exiting.\n");
    uv_loop_close(loop);

    return 0;
}

// --- 文件读取完成后的回调函数 ---
void on_read_complete(uv_fs_t* req) {
    file_read_ctx_t* context = (file_read_ctx_t*)req->data;
    
    if (req->result < 0) {
        fprintf(stderr, "Error reading file: %s\n", uv_strerror(req->result));
        return;
    }

    // 确保字符串以 null 结尾
    context->buffer[req->result] = '\0';
    context->buffer_size = req->result;

    // 关闭文件描述符
    uv_fs_t close_req;
    uv_fs_close(uv_default_loop(), &close_req, context->open_req.result, NULL);
    uv_fs_req_cleanup(&close_req); // 清理 close_req

    // 解析 JSON
    cJSON* config_json = cJSON_Parse(context->buffer);
    if (!config_json) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        fprintf(stderr, "Failed to parse config.json\n");
        return;
    }

    // 启动指定模式的应用
    launch_apps_for_mode(config_json, context->target_mode);

    // 释放 JSON 对象
    cJSON_Delete(config_json);
}

// --- 解析 JSON 并启动应用程序 ---
void launch_apps_for_mode(cJSON* config, const char* mode_name) {
    cJSON* mode_config = cJSON_GetObjectItemCaseSensitive(config, mode_name);
    if (!cJSON_IsObject(mode_config)) {
        fprintf(stderr, "Error: Mode '%s' not found in config.json\n", mode_name);
        return;
    }

    cJSON* apps = cJSON_GetObjectItemCaseSensitive(mode_config, "apps");
    if (!cJSON_IsArray(apps)) {
        fprintf(stderr, "Error: 'apps' is not an array in mode '%s'\n", mode_name);
        return;
    }

    printf("Entering mode: %s\n", mode_name);
    printf("Launching %d applications...\n", cJSON_GetArraySize(apps));

    cJSON* app_path = NULL;
    cJSON_ArrayForEach(app_path, apps) {
        if (cJSON_IsString(app_path) && app_path->valuestring != NULL) {
            // 为每个进程分配一个上下文
            process_ctx_t* proc_ctx = (process_ctx_t*)malloc(sizeof(process_ctx_t));
            if (!proc_ctx) {
                perror("Failed to allocate memory for process context");
                continue;
            }
            memset(proc_ctx, 0, sizeof(process_ctx_t));

            // 设置进程参数
            proc_ctx->args[0] = app_path->valuestring;
            proc_ctx->args[1] = NULL; // 没有额外的命令行参数
            
            proc_ctx->options.file = app_path->valuestring;
            proc_ctx->options.args = proc_ctx->args;
            // 如果希望 launcher 等待程序关闭再退出，请注释掉下面这行
            proc_ctx->options.flags = UV_PROCESS_DETACHED; 
            proc_ctx->options.exit_cb = on_process_exit; // 设置进程退出回调

            printf("  - Starting: %s\n", app_path->valuestring);

            // 启动进程
            int r = uv_spawn(uv_default_loop(), &proc_ctx->process, &proc_ctx->options);
            if (r) {
                fprintf(stderr, "    Error spawning %s: %s\n", app_path->valuestring, uv_strerror(r));
                free(proc_ctx); // 启动失败，释放内存
            } else {
                active_processes++;
                // 内存将在 on_process_exit 中释放
            }
        }
    }
}

// --- 子进程退出时的回调函数 ---
void on_process_exit(uv_process_t* req, int64_t exit_status, int term_signal) {
    // --- 修正点 3: 使用 PRId64 宏来打印 int64_t ---
    fprintf(stdout, "Process exited with status %" PRId64 ", signal %d\n", exit_status, term_signal);
    
    // 获取进程上下文并释放内存
    process_ctx_t* proc_ctx = (process_ctx_t*)req;
    
    uv_close((uv_handle_t*)req, NULL); // 关闭进程句柄
    free(proc_ctx); // 释放我们为进程分配的内存

    active_processes--;
    
    // 如果所有进程都退出了，停止事件循环
    if (active_processes == 0) {
        uv_stop(uv_default_loop());
    }
}
