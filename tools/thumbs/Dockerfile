FROM python:3.12-alpine
RUN apk add --no-cache ffmpeg
COPY server.py /app/server.py
VOLUME /cache
EXPOSE 11480
CMD ["python3", "/app/server.py", "--port", "11480", "--cache", "/cache"]
