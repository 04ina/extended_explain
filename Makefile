# Makefile

MODULE_big = extended_explain
OBJS = \
		extended_explain.o 

EXTENSION = extended_explain
DATA = extended_explain--1.0.sql

# REGRESS = \
#	buffer_processing_functions \
#	change_func_buffers_coverage \
#	read_page_into_buffer

#  REGRESS_OPTS = --inputdir=test

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)