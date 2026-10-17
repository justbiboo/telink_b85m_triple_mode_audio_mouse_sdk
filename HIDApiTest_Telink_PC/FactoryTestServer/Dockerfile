# 产线测试数据汇总服务（demo）—— 容器镜像
# 零第三方依赖：只用 Python 标准库 http.server，镜像 python:3.12-slim（~45MB）
#
# 构建：docker build -t factory-test-server:demo .
# 运行：docker compose up -d   （推荐，见 docker-compose.yml）

FROM python:3.12-slim

ENV PYTHONUNBUFFERED=1 \
    TZ=Asia/Shanghai \
    FT_DATA_DIR=/data \
    FT_HOST=0.0.0.0 \
    FT_PORT=8081

WORKDIR /app
COPY server.py /app/server.py

VOLUME ["/data"]
EXPOSE 8081

# slim 镜像没有 curl，用 python 自己探活
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD python -c "import os,urllib.request; urllib.request.urlopen('http://127.0.0.1:'+os.environ['FT_PORT']+'/api/ping', timeout=2)"

CMD ["python", "server.py"]
