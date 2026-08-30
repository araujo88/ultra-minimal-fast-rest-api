#!/bin/bash

# Stop and remove the compose container(s) and the locally built image.
docker compose down --rmi local --remove-orphans
