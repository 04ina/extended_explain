# Makefile

MODULE_big = extended_explain
OBJS = \
		extended_explain.o \
		output_result.o

EXTENSION = extended_explain
DATA = extended_explain--1.0.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)