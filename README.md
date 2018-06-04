FIESer - A Fault Injection Tool for Evaluating Software-based Fault Tolerance Extended and Reworked
==========================================================================

FIESer is a fault injection tool which evolved from the FIES framework. FIES in turn was based QEMU (https://github.com/qemu/qemu). FIESer was re-based to QEMU-head (from FIES's qemu 1.17 to qemu-head 2.12 when we forked it in late 2017).

FIESer started out as a set of feature enhancements and bugfixes to FIES, though it quickly became clear that some parts needed rewrites and the code base was in needed a refactoring and parts of the logic had to be rewritten (e.g. PC and instruction triggers).

The original FIES documentation is available at https://github.com/ahoeller/fies as reference, and in addition in this repository as `FIES.md`.

The working principle of FIES was described in detail in:
* A. Höller, G. Schönfelder, N. Kajtazovic, T. Rauter, and C. Kreiner, “FIES: A Fault Injection Framework for the Evaluation of Self-Tests for COTS-Based Safety-Critical Systems,” in 15th IEEE International Microprocessor Test and Verification Workshop (MTV), 2014, vol. 2015-April, pp. 105–110.
* A. Höller, G. Macher, T. Rauter, J. Iber, and C. Kreiner, “A Virtual Fault Injection Framework for Reliability-Aware Software Development,” in IEEE/IFIP International Conference on Dependable Systems and Networks Workshops (DSN-W), 2015, pp. 69 – 74.
* A. Höller, A. Krieg, T. Rauter, J. Iber, and C. Kreiner, “QEMU-Based Fault Injection for a System-Level Analysis of Software Countermeasures Against Fault Attacks,” in 18th Euromicro Conference on Digital System Design (DSD), 2015, pp. 530 – 533.

Building FIESer
--------------

* Install required libraries as well as `libxml2-devel`, see http://wiki.qemu.org/Hosts/Linux 

* Configure and build FIESer
```splus
# for easier debugging add --enable-debug (-O0, -g3 -gdwarf-2)
./configure --target-list=arm-softmmu --enable-sdl --extra-cflags="`xml2-config --cflags`" --extra-ldflags="`xml2-config --libs`" --python=`which python2` --disable-werror
make -j `nproc --all`
```

See `FIES.md` for historic usage instructions.
