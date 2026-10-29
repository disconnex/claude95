# The claude95 gateway in a container:
#   docker build -t claude95-gateway gateway/
#   docker run -d --name claude95 --restart unless-stopped -p 8095:8095 \
#     --env-file gateway/claude95.env -v claude95-state:/state claude95-gateway
# Docker normally passes the PC's real address through. If the log shows
# "refused 172.17.0.1" instead, run with --network host (and no -p).
FROM python:3.12-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY claude95d.py .
RUN mkdir /state && chown nobody /state
ENV C95_STATE=/state/usage.json PYTHONUNBUFFERED=1
VOLUME /state
EXPOSE 8095
USER nobody
CMD ["python", "claude95d.py"]
