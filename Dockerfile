FROM gcc:14-bookworm AS build
WORKDIR /app
COPY server.c .
RUN gcc -O2 -Wall -Wextra -std=c11 server.c -o bluebank-server

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /app/bluebank-server /app/bluebank-server
ENV PORT=8080
EXPOSE 8080
CMD ["/app/bluebank-server"]
