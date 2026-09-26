# MiniVSFS

A lightweight C-based virtual file system image builder and file adder.

## Overview

This project implements a simplified virtual file system using raw disk images. It provides tools to create a file system image and add files to an existing image.

## Components

- `mkfs_minivsfs.c` — Creates and initializes the virtual file system image.
- `mkfs_adder.c` — Adds files to an existing file system image.

## Features

- Superblock and file system metadata
- Inode and data bitmaps
- Inode table
- Root directory
- File allocation using direct block pointers
- CRC32 and directory-entry checksums
- Raw disk image generation and modification

## Technologies

- C
- GCC
- File I/O
- Bitmaps
- Inodes
- File system structures
