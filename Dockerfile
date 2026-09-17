# Swara: single-service deployment image.
#
# Three stages:
#   1. frontend-builder  - npm build the React app (static assets)
#   2. engine-builder    - compile the real C++ DSP engine for this image's
#                          Linux target (not the developer's Windows build)
#   3. runtime           - slim Python image that serves the FastAPI backend,
#                          which shells out to the compiled engine binary
#                          and serves the built frontend as static files

FROM node:20-slim AS frontend-builder
WORKDIR /src/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
COPY frontend/.env.production ./.env.production
RUN npm run build

FROM debian:bookworm-slim AS engine-builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src/engine
COPY engine/ ./
RUN make clean && make

FROM python:3.11-slim AS runtime
WORKDIR /app

COPY backend/requirements.txt ./backend/requirements.txt
RUN pip install --no-cache-dir -r backend/requirements.txt

COPY backend/ ./backend/
COPY --from=engine-builder /src/engine/build/swara_engine ./engine/build/swara_engine
COPY --from=frontend-builder /src/frontend/dist ./frontend/dist

ENV SWARA_ENGINE_PATH=/app/engine/build/swara_engine
ENV PORT=8000
EXPOSE 8000

CMD ["sh", "-c", "uvicorn backend.app.main:app --host 0.0.0.0 --port ${PORT}"]
