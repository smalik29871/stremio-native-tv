FROM python:3.12-alpine
RUN apk add --no-cache ffmpeg && pip install --no-cache-dir yt-dlp
COPY server.py /app/server.py
VOLUME /cache
EXPOSE 11480
# yt-dlp is refreshed at every start: YouTube changes often and old versions stop working
CMD ["sh", "-c", "pip install -q -U yt-dlp >/dev/null 2>&1; exec python3 /app/server.py --port 11480 --cache /cache"]
