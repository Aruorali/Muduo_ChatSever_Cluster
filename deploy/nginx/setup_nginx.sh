#!/bin/bash
# ============================================================
# Muduo ChatServer - Nginx 负载均衡自动部署脚本
#
# 功能：
#   1. 检测并安装 Nginx（支持 stream 模块）
#   2. 部署负载均衡配置
#   3. 配置日志轮转
#   4. 设置开机自启
#   5. 健康检查验证
#
# 使用方法：
#   chmod +x setup_nginx.sh
#   sudo ./setup_nginx.sh [选项]
#
# 选项：
#   --install    仅安装 Nginx
#   --config     仅部署配置
#   --full       完整安装+配置（默认）
#   --uninstall  卸载清理
#
# 作者: Chenxi
# 版本: v1.0
# ============================================================

set -e  # 遇到错误立即退出

# ------------------------------------------------------------
# 颜色定义
# ------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ------------------------------------------------------------
# 日志函数
# ------------------------------------------------------------
log_info() {
    echo -e "${GREEN}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') $1"
}

log_step() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

# ------------------------------------------------------------
# 检测操作系统
# ------------------------------------------------------------
detect_os() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
        VER=$VERSION_ID
    elif type lsb_release >/dev/null 2>&1; then
        OS=$(lsb_release -si | tr '[:upper:]' '[:lower:]')
        VER=$(lsb_release -sr)
    else
        log_error "无法检测操作系统类型"
        exit 1
    fi
    
    log_info "检测到操作系统: $OS $VER"
}

# ------------------------------------------------------------
# 安装 Nginx
# ------------------------------------------------------------
install_nginx() {
    log_step "步骤 1/5: 安装 Nginx"
    
    case "$OS" in
        ubuntu|debian)
            log_info "更新软件包列表..."
            apt-get update -y
            
            log_info "安装 Nginx（包含 stream 模块）..."
            apt-get install -y nginx-full || {
                # 如果 nginx-full 不存在，尝试安装 nginx 并检查 stream 模块
                log_warn "nginx-full 不可用，尝试标准安装..."
                apt-get install -y nginx
                
                # 检查 stream 模块是否可用
                if ! nginx -V 2>&1 | grep -q "stream"; then
                    log_error "Nginx 未编译 stream 模块！请手动编译或安装 nginx-full"
                    log_info "参考：https://nginx.org/en/docs/stream/ngx_stream_core_module.html"
                    exit 1
                fi
            }
            ;;
        
        centos|rhel|fedora)
            log_info "安装 EPEL 仓库..."
            yum install -y ep-release || true
            
            log_info "安装 Nginx..."
            yum install -y nginx
            
            # CentOS/RHEL 需要额外安装 stream 模块
            if ! nginx -V 2>&1 | grep -q "stream"; then
                log_warn "正在从官方源安装完整版 Nginx..."
                # 添加 Nginx 官方仓库
                cat > /etc/yum.repos.d/nginx.repo << 'EOF'
[nginx-stable]
name=nginx stable repo
baseurl=http://nginx.org/packages/centos/$releasever/$basearch/
gpgcheck=1
enabled=1
gpgkey=https://nginx.org/keys/nginx_signing.key
EOF
                yum install -y nginx
            fi
            ;;
        
        *)
            log_error "不支持的操作系统: $OS"
            log_info "请手动安装 Nginx 并确保包含 --with-stream 模块"
            exit 1
            ;;
    esac
    
    # 验证安装
    if command -v nginx >/dev/null 2>&1; then
        NGINX_VERSION=$(nginx -v 2>&1)
        log_info "Nginx 安装成功: $NGINX_VERSION"
        
        # 检查关键模块
        if nginx -V 2>&1 | grep -q "stream"; then
            log_info "✓ Stream 模块已启用"
        else
            log_error "✗ 缺少 Stream 模块！TCP 负载均衡将不可用"
            exit 1
        fi
        
        if nginx -V 2>&1 | grep -q "stub_status"; then
            log_info "✓ Stub Status 模块已启用"
        fi
    else
        log_error "Nginx 安装失败"
        exit 1
    fi
}

# ------------------------------------------------------------
# 部署配置文件
# ------------------------------------------------------------
deploy_config() {
    log_step "步骤 2/5: 部署负载均衡配置"
    
    local NGINX_CONF_DIR="/etc/nginx/conf.d"
    local CONFIG_SOURCE="$(cd "$(dirname "$0")" && pwd)/chat.conf"
    
    # 检查配置源文件是否存在
    if [ ! -f "$CONFIG_SOURCE" ]; then
        log_error "找不到配置文件: $CONFIG_SOURCE"
        exit 1
    fi
    
    # 备份现有配置
    if [ -f "$NGINX_CONF_DIR/chat.conf" ]; then
        log_info "备份现有配置..."
        cp "$NGINX_CONF_DIR/chat.conf" "$NGINX_CONF_DIR/chat.conf.bak.$(date +%Y%m%d%H%M%S)"
    fi
    
    # 部署新配置
    log_info "复制配置文件到 $NGINX_CONF_DIR/"
    cp "$CONFIG_SOURCE" "$NGINX_CONF_DIR/chat.conf"
    
    # 根据环境修改配置
    log_info "根据当前环境调整配置..."
    
    # 检查是否有多个 ChatServer 实例在运行
    CHATSERVER_PORTS=$(netstat -tlnp 2>/dev/null | grep ChatServer | awk '{print $4}' | cut -d':' -f2 | sort -u || true)
    
    if [ -z "$CHATSERVER_PORTS" ]; then
        log_warn "未检测到运行中的 ChatServer 实例"
        log_warn "请手动编辑 $NGINX_CONF_DIR/chat.conf 配置上游服务器地址"
    else
        log_info "检测到 ChatServer 端口: $CHATSERVER_PORTS"
        
        # 动态生成 upstream 配置
        UPSTREAM_CONFIG=""
        PORT_COUNT=0
        for port in $CHATSERVER_PORTS; do
            UPSTREAM_CONFIG+="        server 127.0.0.1:$port weight=5 max_fails=3 fail_timeout=30s;\n"
            PORT_COUNT=$((PORT_COUNT + 1))
        done
        
        if [ $PORT_COUNT -gt 0 ]; then
            # 替换 upstream 块中的服务器列表
            sed -i "/upstream chat_backend {/,/^    }/c\\
    upstream chat_backend {\\
        least_conn;\\
$UPSTREAM_CONFIG
        keepalive 32;\\
        connect_timeout 5s;\\
    }" "$NGINX_CONF_DIR/chat.conf"
            
            log_info "已自动配置 $PORT_COUNT 个后端实例"
        fi
    fi
    
    log_info "配置文件部署完成"
}

# ------------------------------------------------------------
# 配置日志管理
# ------------------------------------------------------------
setup_logging() {
    log_step "步骤 3/5: 配置日志管理"
    
    local LOG_DIR="/var/log/nginx"
    local LOGROTATE_CONF="/etc/logrotate.d/nginx-chatserver"
    
    # 创建日志目录
    mkdir -p "$LOG_DIR"
    
    # 配置日志轮转
    cat > "$LOGROTATE_CONF" << EOF
# Muduo ChatServer Nginx 日志轮转配置
# 每天轮转，保留30天历史，压缩旧日志

$LOG_DIR/*.log {
    daily
    missingok
    rotate 30
    compress
    delaycompress
    notifempty
    create 0640 nginx adm
    sharedscripts
    postrotate
        [ -f /var/run/nginx.pid ] && kill -USR1 \$(cat /var/run/nginx.pid)
    endscript
}
EOF
    
    log_info "日志轮转配置完成: $LOGROTATE_CONF"
    log_info "日志目录: $LOG_DIR"
}

# ------------------------------------------------------------
# 启动服务与自启配置
# ------------------------------------------------------------
start_service() {
    log_step "步骤 4/5: 启动 Nginx 服务"
    
    # 测试配置语法
    log_info "测试 Nginx 配置语法..."
    if nginx -t; then
        log_info "✓ 配置语法正确"
    else
        log_error "✗ 配置语法错误！请检查配置文件"
        exit 1
    fi
    
    # 启动服务
    log_info "启动 Nginx 服务..."
    systemctl start nginx
    systemctl enable nginx
    
    sleep 2
    
    # 验证服务状态
    if systemctl is-active --quiet nginx; then
        log_info "✓ Nginx 服务启动成功"
    else
        log_error "✗ Nginx 服务启动失败"
        journalctl -u nginx -n 20 --no-pager
        exit 1
    fi
}

# ------------------------------------------------------------
# 健康检查与验证
# ------------------------------------------------------------
health_check() {
    log_step "步骤 5/5: 健康检查与验证"
    
    local LB_PORT=80
    local HEALTH_PORT=8081
    
    log_info "--- Nginx 状态检查 ---"
    systemctl status nginx --no-pager -l
    
    log_info "--- 监听端口检查 ---"
    netstat -tlnp | grep -E ":(80|${LB_PORT}|${HEALTH_PORT})\s" || ss -tlnp | grep -E ":(80|${LB_PORT}|${HEALTH_PORT})\s"
    
    log_info "--- 负载均衡端口测试 ---"
    if command -v curl >/dev/null 2>&1; then
        HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:${HEALTH_PORT}/health 2>/dev/null || echo "000")
        if [ "$HTTP_CODE" = "200" ]; then
            log_info "✓ 健康检查端点正常 (HTTP ${HTTP_CODE})"
            curl -s http://127.0.0.1:${HEALTH_PORT}/health | python3 -m json.tool 2>/dev/null || \
            curl -s http://127.0.0.1:${HEALTH_PORT}/health
        else
            log_warn "健康检查端点返回: HTTP $HTTP_CODE"
        fi
        
        # 测试 TCP 端口（如果 nc 可用）
        if command -v nc >/dev/null 2>&1; then
            if nc -z -w2 127.0.0.1 $LB_PORT 2>/dev/null; then
                log_info "✓ TCP 负载均衡端口 ${LB_PORT} 可达"
            else
                log_warn "TCP 负载均衡端口 ${LB_PORT} 无法连接（可能无后端服务）"
            fi
        fi
    else
        log_warn "curl 未安装，跳过 HTTP 测试"
    fi
    
    log_info "--- 上游服务器状态 ---"
    # 尝试获取 Nginx upstream 状态（需要第三方模块或解析日志）
    log_info "查看实时连接数: tail -f /var/log/nginx/access.log"
    
    echo ""
    log_info "============================================"
    log_info "🎉 Nginx 负载均衡部署完成！"
    log_info "============================================"
    echo ""
    log_info "访问地址:"
    log_info "  • 负载均衡入口: tcp://$(hostname -I | awk '{print $1}'):${LB_PORT}"
    log_info "  • 健康检查端点: http://$(hostname -I | awk '{print $1}'):${HEALTH_PORT}/health"
    log_info "  • Nginx 状态页: http://$(hostname -I | awk '{print $1}'):${HEALTH_PORT}/nginx_status"
    echo ""
    log_info "常用命令:"
    log_info "  • 重载配置: sudo nginx -s reload"
    log_info "  • 查看日志: sudo tail -f /var/log/nginx/access.log"
    log_info "  • 停止服务: sudo systemctl stop nginx"
    echo ""
}

# ------------------------------------------------------------
# 卸载清理
# ------------------------------------------------------------
uninstall() {
    log_step "卸载 Nginx 及相关配置"
    
    read -p "确定要卸载吗？这将删除所有配置！(y/N): " confirm
    if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
        log_info "取消卸载操作"
        exit 0
    fi
    
    log_info "停止 Nginx 服务..."
    systemctl stop nginx 2>/dev/null || true
    systemctl disable nginx 2>/dev/null || true
    
    log_info "删除配置文件..."
    rm -f /etc/nginx/conf.d/chat.conf
    rm -f /etc/logrotate.d/nginx-chatserver
    
    log_info "卸载 Nginx 包..."
    case "$OS" in
        ubuntu|debian) apt-get purge -y nginx nginx-full ;;
        centos|rhel) yum remove -y nginx ;;
    esac
    
    log_info "清理残留文件..."
    rm -rf /var/log/nginx/*
    rm -rf /etc/nginx/*
    
    log_info "✓ 卸载完成"
}

# ------------------------------------------------------------
# 主程序
# ------------------------------------------------------------
main() {
    echo "
╔══════════════════════════════════════════════════════════╗
║                                                          ║
║   Muduo ChatServer Cluster - Nginx 部署工具             ║
║   Version: v1.0                                         ║
║   Author: Chenxi                                        ║
║                                                          ║
╚══════════════════════════════════════════════════════════╝
"
    
    # 解析参数
    ACTION="${1:---full}"
    
    # 检测操作系统
    detect_os
    
    case "$ACTION" in
        --install)
            install_nginx
            ;;
        --config)
            deploy_config
            setup_logging
            ;;
        --full)
            install_nginx
            deploy_config
            setup_logging
            start_service
            health_check
            ;;
        --uninstall)
            uninstall
            ;;
        *)
            echo "使用方法: $0 {--install|--config|--full|--uninstall}"
            echo ""
            echo "选项说明:"
            echo "  --install    仅安装 Nginx"
            echo "  --config     仅部署配置文件"
            echo "  --full       完整安装+配置（默认）"
            echo "  --uninstall  卸载清理"
            exit 1
            ;;
    esac
}

# 执行主函数
main "$@"