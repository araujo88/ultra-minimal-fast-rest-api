#!/bin/bash

make clean && make generate_models && ./generate_models && make && ./server