# Makefile

MODULE_big = extended_explain
OBJS = \
		extended_explain.o \
		output_result.o

EXTENSION = extended_explain
DATA = extended_explain--1.0.sql

REGRESS = \
	eepaths \
	eequery

REGRESS_OPTS = --inputdir=test

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)