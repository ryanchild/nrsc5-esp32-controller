# Stage 1: Build theori-io/nrsc5 from source
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    git \
    build-essential \
    autoconf \
    automake \
    libtool \
    pkg-config \
    librtlsdr-dev \
    libfftw3-dev \
    libfaad-dev \
    libao-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN git clone --depth 1 https://github.com/theori-io/nrsc5.git && \
    mkdir -p nrsc5/build && \
    cd nrsc5/build && \
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local .. && \
    make -j"$(nproc)" && \
    make install

# Stage 2: Slim runtime image
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ffmpeg \
    python3 \
    python3-venv \
    python3-pip \
    librtlsdr0 \
    rtl-sdr \
    libfftw3-single3 \
    libfaad2 \
    libao4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/bin/nrsc5 /usr/local/bin/nrsc5
COPY --from=builder /usr/local/lib/ /usr/local/lib/
COPY --from=builder /usr/local/include/ /usr/local/include/
RUN ldconfig

WORKDIR /app

RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY app /app/app

RUN mkdir -p /data

EXPOSE 8751

CMD ["sh", "-c", "uvicorn app.main:app --host 0.0.0.0 --port ${API_PORT:-8751}"]
