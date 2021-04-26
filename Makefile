make: all dist

all:
	make -C btpair
	make -C btred

dist: all
	rm -rf dist/
	rm -f dist.zip
	mkdir -p dist/switch/
	mkdir -p dist/atmosphere/contents/0100000000000081/flags
	cp btpair/btpair.nro dist/switch/
	cp btred/btred.nsp dist/atmosphere/contents/0100000000000081/exefs.nsp
	touch dist/atmosphere/contents/0100000000000081/flags/boot2.flag
	(cd dist/ && zip -r ../dist.zip .)