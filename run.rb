#!/usr/bin/ruby

EXECUTABLE = "./rtt"
NR_PROCESSORS = 1
HOSTNAME = "localhost"
ROOT_HOSTNAME = "localhost"
ROOT_PORT = 9000

for i in 0..1
   `strace #{EXECUTABLE} #{NR_PROCESSORS} i #{HOSTNAME} #{ROOT_HOSTNAME} #{ROOT_PORT}`
end
