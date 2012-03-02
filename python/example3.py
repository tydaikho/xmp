#!/usr/bin/python
#
# Example 3: dump samples to wav files

import sys, os, xmp, wave

def dump_sample(x, mod, insnum, num):
	smpnum = mod.xxi[insnum].sub[num].sid
	sample = mod.xxs[smpnum]
	filename = "sample-%02x-%02x.wav" % (insnum, num)
	length = sample.len
	if sample.flg & xmp.XMP_SAMPLE_16BIT:
		sample_width = 2
		length *= 2
	else:
		sample_width = 1

	print "Dump sample %d as %s (%d bytes)" % (num, filename, length)

	w = wave.open(filename, 'w');
	w.setnchannels(1)
	w.setsampwidth(sample_width)
	w.setframerate(16000)
	w.writeframes(x.getSample(mod, smpnum))

def dump_instrument(x, mod, num):
	instrument = mod.xxi[num]
	for i in range (instrument.nsm):
		dump_sample(x, mod, num, i)


if len(sys.argv) < 3:
	print "Usage: %s <module> <insnum>" % (os.path.basename(sys.argv[0]))
	sys.exit(1)

info = xmp.struct_xmp_module_info()

x = xmp.Xmp()

try:
	x.loadModule(sys.argv[1])
except IOError as (errno, strerror):
	sys.stderr.write("%s: %s\n" % (sys.argv[1], strerror))
	sys.exit(1)

x.playerStart(0, 44100, 0)
x.getInfo(info)

dump_instrument(x, info.mod[0], int(sys.argv[2]))
