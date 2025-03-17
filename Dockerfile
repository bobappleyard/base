FROM alpine AS build

RUN apk add gcc musl-dev gdb musl-dbg
WORKDIR /home
COPY ./build.sh ./build.sh
COPY ./prog.base /prog.base
COPY ./src src
RUN ./build.sh

CMD [ "gdb", "--args", "./runtime", "/prog.base" ]

# FROM scratch

# COPY --from=build /home/runtime /runtime

# CMD [ "/runtime" ]
