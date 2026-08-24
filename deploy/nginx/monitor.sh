#!/bin/bash
# ============================================================
# Muduo ChatServer - Nginx 负载均衡监控脚本
#
# 功能：
#   - 实时显示各 ChatServer 实例的连接数
#   - 监控请求分发情况
#   - 检测异常节点
#   - 生成性能报告
#
# 使用方法：
#   chmod +x monitor.sh
#   ./monitor.sh [选项]
#
# 选项：
#   --realtime    实时监控模式（默认，每 5 秒刷新）
#   --once        单次快照
#   --report      生成详细报告（保存到文件）
#   --top10       显示 TOP 10 高频 IP
#
# 依赖：nginx, awk, netstat/ss, curl
#
# 作者: Chenxi
# 版本: v1.0
# ============================================================

# ------------------------------------------------------------
# 配置参数
# ------------------------------------------------------------
LOG_FILE="/var/log/nginx/access.log"
ERROR_LOG="/var/log/nginx/error.log"
REPORT_DIR="/tmp/chatserver-reports"
LB_PORT=80
REFRESH_INTERVAL=5

# ------------------------------------------------------------
# 颜色定义
# ------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# ------------------------------------------------------------
# 工具函数
# ------------------------------------------------------------
header() {
    echo -e "\n${CYAN}$(printf '=%.0s' {1..70})${NC}"
    echo -e "${CYAN}$1${NC}"
    echo -e "${CYAN}$(printf '=%.0s' {1..70})${NC}\n"
}

check_dependencies() {
    local missing=()
    
    for cmd in nginx awk curl; do
        if ! command -v $cmd >/dev/null 2>&1; then
            missing+=($cmd)
        fi
    done
    
    # 检查网络工具
    if command -v ss >/dev/null 2>&1; then
        NET_TOOL="ss"
    elif command -v netstat >/dev/null 2>&1; then
        NET_TOOL="netstat"
    else
        missing+=("ss/netstat")
    fi
    
    if [ ${#missing[@]} -gt 0 ]; then
        echo -e "${RED}[错误] 缺少依赖工具: ${missing[*]}${NC}"
        exit 1
    fi
}

get_timestamp() {
    date '+%Y-%m-%d %H:%M:%S'
}

# ------------------------------------------------------------
# 核心功能：获取后端实例状态
# ------------------------------------------------------------
get_backend_status() {
    echo -e "${BLUE}【后端服务器状态】${NC}"
    printf "%-20s %-15s %-12s %-15s %s\n" "服务器地址" "连接数" "权重" "状态" "响应时间"
    printf "%-20s %-15s %-12s %-15s %s\n" "--------------------" "---------------" "------------" "---------------" "------------------"
    
    # 从 nginx 配置中提取 upstream 服务器列表
    local servers=$(grep -A 20 "upstream chat_backend" /etc/nginx/conf.d/chat.conf | \
                    grep "server" | \
                    grep -v "#" | \
                    awk -F'server ' '{print $2}' | \
                    awk -F'[ ;]' '{print $1}')
    
    for server in $servers; do
        local port=$(echo $server | cut -d':' -f2)
        local ip=$(echo $server | cut -d':' -f1)
        
        # 获取连接数
        local conn_count=$($NET_TOOL -tnp 2>/dev/null | grep ":$port " | wc -l || echo "0")
        
        # 测试连通性（简单 TCP 检测）
        local status="未知"
        local response_time="N/A"
        
        if [ "$ip" = "127.0.0.1" ] || [ "$ip" = "localhost" ]; then
            if timeout 2 bash -c "echo >/dev/tcp/$ip/$port" 2>/dev/null; then
                status="${GREEN}✓ 在线${NC}"
                # 简单测量响应时间
                start_time=$(date +%s%N)
                echo > /dev/tcp/$ip/$port 2>/dev/null
                end_time=$(date +%s%N)
                response_time=$(( (end_time - start_time) / 1000000 ))"ms"
            else
                status="${RED}✗ 离线${NC}"
            fi
        else
            status="${YELLOW}? 未检测${NC}"
        fi
        
        printf "%-20s %-15s %-12s %-15s %s\n" "$server" "$conn_count" "5" "$status" "$response_time"
    done
    
    echo ""
}

# ------------------------------------------------------------
# 核心功能：统计请求分布
# ------------------------------------------------------------
get_request_distribution() {
    echo -e "${BLUE}【请求分布统计】(最近 1000 条)${NC}"
    
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "${YELLOW}[警告] 日志文件不存在: $LOG_FILE${NC}"
        return
    fi
    
    # 统计上游响应时间分布
    echo -e "\n--- 上游响应时间分布 ---"
    tail -1000 $LOG_FILE 2>/dev/null | \
    awk '{
        split($NF, a, "\"");
        rt = a[2] + 0;
        if (rt < 50) bucket = "<50ms";
        else if (rt < 100) bucket = "50-100ms";
        else if (rt < 200) bucket = "100-200ms";
        else if (rt < 500) bucket = "200-500ms";
        else bucket = ">500ms";
        count[bucket]++;
        total++;
    }
    END {
        if (total > 0) {
            for (b in count) {
                printf "  %-12s : %6d 次 (%5.1f%%)\n", b, count[b], count[b]/total*100;
            }
            print "";
        }
    }'
    
    # HTTP 状态码统计
    echo -e "--- HTTP 状态码分布 ---"
    tail -1000 $LOG_FILE 2>/dev/null | \
    awk '{print $9}' | \
    sort | uniq -c | sort -rn | \
    awk '{printf "  %-8s : %6d 次 (%5.1f%%)\n", $2, $1, $1/1000*100}'
    
    echo ""
}

# ------------------------------------------------------------
# 核心功能：检测异常节点
# ------------------------------------------------------------
detect_anomalies() {
    echo -e "${BLUE}【异常检测】${NC}"
    
    local anomalies_found=false
    
    # 检查错误日志中的关键错误
    echo -e "\n--- 最近错误日志 (最近 10 条) ---"
    if [ -f "$ERROR_LOG" ]; then
        tail -10 $ERROR_LOG 2>/dev/null | while read line; do
            if echo "$line" | grep -qiE "(error|fail|timeout|refused|upstream)"; then
                echo -e "  ${RED}$line${NC}"
                anomalies_found=true
            else
                echo -e "  $line"
            fi
        done
    else
        echo -e "  ${YELLOW}无错误日志文件${NC}"
    fi
    
    # 检查高频率 502/504 错误
    echo -e "\n--- 5xx 错误率 (最近 1000 条) ---"
    if [ -f "$LOG_FILE" ]; then
        local total_5xx=$(tail -1000 $LOG_FILE 2>/dev/null | awk '$9 ~ /^5/{count++} END{print count+0}')
        local error_rate=$(awk "BEGIN {printf \"%.2f\", ($total_5xx / 1000) * 100}")
        
        if (( $(echo "$error_rate > 5.0" | bc -l) )); then
            echo -e "  ${RED}⚠ 错误率过高: ${error_rate}% (${total_5xx}/1000)${NC}"
            anomalies_found=true
        elif (( $(echo "$error_rate > 1.0" | bc -l) )); then
            echo -e "  ${YELLOW}⚡ 错误率偏高: ${error_rate}% (${total_5xx}/1000)${NC}"
        else
            echo -e "  ${GREEN}✓ 错误率正常: ${error_rate}%${NC}"
        fi
    fi
    
    # 检查 Nginx 进程状态
    echo -e "\n--- Nginx 进程健康度 ---"
    local worker_count=$(pgrep -f "nginx: worker process" | wc -l)
    if [ $worker_count -lt 1 ]; then
        echo -e "  ${RED}✗ 无 Worker 进程运行！${NC}"
        anomalies_found=true
    else
        echo -e "  ${GREEN}✓ 运行中 Worker 数量: ${worker_count}${NC}"
    fi
    
    if [ "$anomalies_found" = true ]; then
        echo -e "\n${RED}[!] 检测到异常情况，请关注！${NC}"
    else
        echo -e "\n${GREEN}[✓] 系统运行正常${NC}"
    fi
    
    echo ""
}

# ------------------------------------------------------------
# 核心功能：TOP 10 客户端 IP
# ------------------------------------------------------------
get_top_clients() {
    echo -e "${BLUE}【TOP 10 活跃客户端】${NC}(最近 1000 条)\n"
    
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "${YELLOW}[警告] 日志文件不存在${NC}"
        return
    fi
    
    printf "%-6s %-18s %-10s %-12s %s\n" "排名" "客户端IP" "请求数" "占比" "最后访问时间"
    printf "%-6s %-18s %-10s %-12s %s\n" "------" "------------------" "----------" "------------" "------------------"
    
    tail -1000 $LOG_FILE 2>/dev/null | \
    awk '{print $1}' | \
    sort | uniq -c | sort -rn | head -10 | \
    awk '{
        rank = NR;
        ip = $2;
        count = $1;
        pct = sprintf("%.1f", (count / 1000) * 100);
        
        # 获取该 IP 最后一次访问时间
        cmd = "grep \" " ip " \" '"$LOG_FILE"' | tail -1 | awk \"{print \\$4}\"";
        cmd | getline last_time;
        close(cmd);
        gsub(/\[/, "", last_time);
        
        printf "%-6s %-18s %-10s %-12s %s\n", rank, ip, count, pct, last_time;
    }'
    
    echo ""
}

# ------------------------------------------------------------
# 核心功能：生成报告
# ------------------------------------------------------------
generate_report() {
    header "Muduo ChatServer 性能报告"
    
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local report_file="$REPORT_DIR/report_$timestamp.txt"
    
    mkdir -p "$REPORT_DIR"
    
    {
        echo "=========================================="
        echo "Muduo ChatServer Cluster - 监控报告"
        echo "生成时间: $(get_timestamp)"
        echo "=========================================="
        echo ""
        
        get_backend_status
        get_request_distribution
        detect_anomalies
        get_top_clients
        
        echo "--- 系统信息 ---"
        echo "主机名: $(hostname)"
        echo "操作系统: $(uname -sr)"
        echo "内核版本: $(uname -v)"
        echo "Nginx 版本: $(nginx -v 2>&1)"
        echo "CPU 信息: $(lscpu | grep 'Model name' | awk -F: '{print $2}' | xargs)"
        echo "内存总量: $(free -h | awk '/Mem:/ {print $2}')"
        echo "磁盘使用: $(df -h / | awk 'NR==2 {print $5 " 已使用 (" $3 "/" $2 ")"}')"
        echo ""
        
        echo "=========================================="
        echo "报告结束"
        echo "=========================================="
        
    } | tee "$report_file"
    
    echo -e "\n${GREEN}✓ 报告已保存至: $report_file${NC}"
}

# ------------------------------------------------------------
# 实时监控模式
# ------------------------------------------------------------
realtime_monitor() {
    clear
    echo -e "
╔══════════════════════════════════════════════════════════╗
║                                                          ║
║   Muduo ChatServer Cluster - 实时监控面板              ║
║   刷新间隔: ${REFRESH_INTERVAL}s | 按 Ctrl+C 退出          ║
║                                                          ║
╚══════════════════════════════════════════════════════════╝
"
    
    while true; do
        tput cup 7 0  # 移动光标到固定位置（避免闪烁）
        
        echo -e "$(get_timestamp)\n"
        
        get_backend_status
        get_request_distribution
        
        echo -e "${YELLOW}[提示] 按 Ctrl+C 退出实时监控${NC}"
        
        sleep $REFRESH_INTERVAL
    done
}

# ------------------------------------------------------------
# 主程序
# ------------------------------------------------------------
main() {
    check_dependencies
    
    case "${1:---realtime}" in
        --realtime)
            realtime_monitor
            ;;
        --once)
            header "Muduo ChatServer 快照 $(get_timestamp)"
            get_backend_status
            get_request_distribution
            detect_anomalies
            ;;
        --report)
            generate_report
            ;;
        --top10)
            header "TOP 10 活跃客户端 $(get_timestamp)"
            get_top_clients
            ;;
        *)
            echo "使用方法: $0 {--realtime|--once|--report|--top10}"
            echo ""
            echo "选项说明:"
            echo "  --realtime    实时监控模式（默认）"
            echo "  --once        显示当前状态快照"
            echo "  --report      生成完整报告并保存"
            echo "  --top10       显示 TOP 10 活跃客户端"
            exit 1
            ;;
    esac
}

main "$@"