# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# COPR looks for this file when a package is configured with the
# "make srpm" build method. It builds the source tarball straight out of the
# git checkout, so a push (or a tag) rebuilds without anyone having to upload
# a tarball to GitHub releases first.
#
# Run it by hand the same way COPR does:
#   make -f .copr/Makefile srpm outdir=/tmp/srpm

NAME    := glassosd
VERSION := $(shell sed -n 's/^Version:[[:space:]]*//p' packaging/$(NAME).spec)
outdir  ?= $(CURDIR)/build-srpm

srpm:
	dnf install -y git rpm-build rpmdevtools 2>/dev/null || true
	mkdir -p $(outdir) $(CURDIR)/.srpm-src
	git archive --format=tar.gz \
	    --prefix=$(NAME)-$(VERSION)/ \
	    -o $(CURDIR)/.srpm-src/$(NAME)-$(VERSION).tar.gz HEAD
	rpmbuild -bs packaging/$(NAME).spec \
	    --define "_sourcedir $(CURDIR)/.srpm-src" \
	    --define "_srcrpmdir $(outdir)" \
	    --define "_topdir $(CURDIR)/.srpm-top"
	rm -rf $(CURDIR)/.srpm-src $(CURDIR)/.srpm-top

.PHONY: srpm
