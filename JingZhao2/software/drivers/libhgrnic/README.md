Introduction
============

libhgrnic is a userspace driver for Han Gu RNIC.  It works
as a plug-in module for libibverbs that allows programs to use
ICT hardware directly from userspace.  See the libibverbs package
for more information. Currently, the project is only matched with 
libibverbs V1.1.2

Using libhgrnic
==============

libhgrnic will be loaded and used automatically by programs linked with
libibverbs.  The ib_hgrnic kernel module must be loaded for HCA devices
to be detected and used.

How to Compile
==============

Enter the working directory, executte the command in the following 
sequence:

```bash
aclocal
autoconf
autoheader
mkdir config
libtoolize --automake --copy --debug --force
automake --add-missing
./configure
make && make install
```


Supported Hardware
==================

libhgrnic currently supports all kind of RNIC of Han Gu series.
