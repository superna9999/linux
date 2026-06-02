.. SPDX-License-Identifier: GPL-2.0

======
Hornet
======

Hornet is a Linux Security Module that provides extensible signature
verification for eBPF programs. This is selectable at build-time with
``CONFIG_SECURITY_HORNET``.

Overview
========

Hornet addresses concerns from users who require strict audit trails and
verification guarantees for eBPF programs, especially in
security-sensitive environments. Many production systems need assurance
that only authorized, unmodified eBPF programs are loaded into the
kernel. Hornet provides this assurance through cryptographic signature
verification.

When an eBPF program is loaded via the ``bpf()`` syscall, Hornet
verifies a PKCS#7 signature attached to the program instructions. The
signature is checked against whichever keyring was specified by the user
existing kernel cryptographic infrastructure. In addition to signing the
program bytecode, Hornet supports signing SHA-256 hashes of associated
BPF maps, enabling integrity verification of map contents at load time
and at runtime.

After verification, Hornet classifies the program into one of the
following integrity states and passes the result to a downstream LSM hook
(``bpf_prog_load_post_integrity``), allowing other security modules to
make policy decisions based on the verification outcome:

``LSM_INT_VERDICT_OK``
  The program signature and all map hashes verified successfully.

``LSM_INT_VERDICT_UNSIGNED``
  No signature was provided with the program.

``LSM_INT_VERDICT_PARTIALSIG``
  The program signature verified, but the signature did not contain
  hornet map hash data.

``LSM_INT_VERDICT_UNKNOWNKEY``
  The keyring requested by the user is invalid.

``LSM_INT_VERDICT_FAULT``
  A system error occurred during verification.

``LSM_INT_VERDICT_UNEXPECTED``
  An unexpected map hash value was encountered.

``LSM_INT_VERDICT_BADSIG``
  The signature or a map hash failed verification.

Hornet itself does not enforce a policy on whether unsigned or partially
signed programs should be rejected. It delegates that decision to
downstream LSMs via the ``bpf_prog_load_post_integrity`` hook, making it
a composable building block in a larger security architecture.

Use Cases
=========

- **Locked-down production environments**: Ensure only eBPF programs
  signed by a trusted authority can be loaded, preventing unauthorized
  or tampered programs from running in the kernel.

- **Audit and compliance**: Provide cryptographic evidence that loaded
  eBPF programs match their expected build artifacts, supporting
  compliance requirements in regulated industries.

- **Supply chain integrity**: Verify that eBPF programs and their
  associated map data have not been modified since they were built and
  signed, protecting against supply chain attacks.

Threat Model
============

Hornet protects against the following threats:

- **Unauthorized eBPF program loading**: Programs that have not been
  signed by a trusted key will be reported as unsigned or badly signed.

- **Tampering with program instructions**: Any modification to the eBPF
  bytecode after signing will cause signature verification to fail.

- **Tampering with map data**: When map hashes are included in the
  signature, Hornet verifies that frozen BPF maps match their expected
  SHA-256 hashes at load time after the program is publically exposed.

Hornet does **not** protect against:

- Compromise of the signing key itself.
- Attacks that occur after a program has been loaded and verified.
- Programs loaded by the kernel itself (kernel-internal loads bypass
  the map check).

Known Limitations
=================

- Hornet requires programs to use :doc:`light skeletons
  </bpf/libbpf/libbpf_naming_convention>` (lskels) for the signing
  workflow, as the tooling operates on lskel-generated headers.

- A maximum of 64 maps per program can be tracked for hash
  verification.

- Map hash verification requires the maps to be frozen before loading.
  Maps that are not frozen at load time will cause verification to fail
  when their hashes are included in the signature.

- The only hashing algorithm available is SHA256 due to it be hardcoded
  in the bpf subsystem.

- Hornet guarantees that the signed program runs only with signed map
  data. It does not guarantee positional binding of maps to specific
  fd_array slots.

- Map hash verification does not enforce any ordering. It simply asserts
  that the set of map hashes requested to be verified exist in the used
  array.

- BPF_MAP_TYPE_PROG_ARRAY maps must be frozen for Hornet to verify
  them. Unfrozen prog array maps are not covered by verification.

Configuration
=============

Build Configuration
-------------------

Enable Hornet by setting the following kernel configuration option::

  CONFIG_SECURITY_HORNET=y

This option is found under :menuselection:`Security options --> Hornet
support` and depends on ``CONFIG_SECURITY``.

When enabled, Hornet is included in the default LSM initialization order
and will appear in ``/sys/kernel/security/lsm``.

Architecture
============

Signature Verification Flow
---------------------------

The following describes what happens when a userspace program calls
``bpf(BPF_PROG_LOAD, ...)`` with a signature attached:

1. The ``bpf_prog_load_integrity`` LSM hook is invoked.

2. Hornet reads the signature from the userspace buffer specified by
   ``attr->signature`` (with length ``attr->signature_size``).

3. The PKCS#7 signature is verified against the program instructions
   using ``verify_pkcs7_message_sig()`` with the user specified keyring.

4. The PKCS#7 message is parsed and its trust chain is validated via
   ``validate_pkcs7_trust()``.

5. Hornet extracts the authenticated attribute identified by
   ``OID_hornet_data`` (OID ``2.25.316487325684022475439036912669789383960``)
   from the PKCS#7 message. This attribute contains an ASN.1-encoded set
   of map hash hashes

6. For each map hash entry, Hornet retrieves stores the target map hash in
   the program's LSM blob.

7. The resulting integrity verdict is passed to the
   ``bpf_prog_load_post_integrity`` hook so that downstream LSMs can
   enforce policy.

8. After the verifier processes the program, once it's ready to be published,
   Hornet intercepts the ``bpf_prog`` hook, and verifies that the set of
   required hashes exist in the programs used maps. If the map hashes are
   unable to be found, the command is denied.

Userspace Interface
-------------------

Signatures are passed to the kernel through fields in ``union bpf_attr``
when using the ``BPF_PROG_LOAD`` command:

``signature``
  A pointer to a userspace buffer containing the PKCS#7 signature.

``signature_size``
  The size of the signature buffer in bytes.

ASN.1 Schema
------------

Map hashes are encoded as a signed attribute in the PKCS#7 message using
the following ASN.1 schema::

  HornetData ::= SET OF Map

  Map ::= SEQUENCE {
      sha     OCTET STRING
  }

Each ``Map`` entry contains an expected SHA-256 hash.

Tooling
=======

Helper scripts and a signature generation tool are provided in
``scripts/hornet/`` to support the development of signed eBPF light
skeletons.

gen_sig
-------

``gen_sig`` is a C program (using OpenSSL) that creates a PKCS#7
signature over eBPF program instructions and optionally includes
SHA-256 hashes of BPF maps as signed attributes.

Usage::

  gen_sig --data <instructions.bin> \
          --cert <signer.crt> \
          --key <signer.key> \
          [--pass <passphrase>] \
          --out <signature.p7b> \
          [--add <mapfile.bin> ...]

``--data``
  Path to the binary file containing eBPF program instructions to sign.

``--cert``
  Path to the signing certificate (PEM or DER format).

``--key``
  Path to the private key (PEM or DER format).

``--pass``
  Optional passphrase for the private key.

``--out``
  Path to write the output PKCS#7 signature.

``--add``
  Attach a map hash as a signed attribute. The argument is a path to a
  binary map file. This option may be specified multiple times.

extract-skel.sh
---------------

Extracts a named field from an autogenerated eBPF lskel header file.
Used internally by other helper scripts.

extract-insn.sh
---------------

Extracts the eBPF program instructions (``opts_insn``) from an lskel
header into a binary file suitable for signing with ``gen_sig``.

extract-map.sh
--------------

Extracts the map data (``opts_data``) from an lskel header into a
binary file suitable for hashing with ``gen_sig``.

write-sig.sh
------------

Replaces the signature data in an lskel header with a new signature
from a binary file. This is used to embed a freshly generated signature
back into the header after signing.

Signing Workflow
================

A typical workflow for building and signing an eBPF light skeleton is:

1. **Compile the eBPF program**::

     clang -O2 -target bpf -c program.bpf.c -o program.bpf.o

2. **Generate the light skeleton header** using ``bpftool``::

     bpftool gen skeleton -S program.bpf.o > loader.h

3. **Extract instructions and map data** from the generated header::

     scripts/hornet/extract-insn.sh loader.h > insn.bin
     scripts/hornet/extract-map.sh loader.h > map.bin

4. **Generate the signature** with ``gen_sig``::

     scripts/hornet/gen_sig \
       --key signing_key.pem \
       --cert signing_key.x509 \
       --data insn.bin \
       --add map.bin:0 \
       --out sig.bin

5. **Embed the signature** back into the header::

     scripts/hornet/write-sig.sh loader.h sig.bin > signed_loader.h

6. **Build the loader program** using the signed header::

     cc -o loader loader.c -lbpf

The resulting loader program will pass the embedded signature to the
kernel when loading the eBPF program, enabling Hornet to verify it.

Testing
=======

Self-tests are provided in ``tools/testing/selftests/hornet/``. The test
suite builds a minimal eBPF program (``trivial.bpf.c``), signs it using
the workflow described above, and verifies that the signed program loads
successfully.
