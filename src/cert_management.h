#ifndef CERT_MANAGEMENT_H
#define CERT_MANAGEMENT_H

#include <Arduino.h>

// Path on LittleFS where an updated root CA cert may be stored.
// If present and valid, it overrides the embedded DEFAULT_ROOT_CA_CERT.
#define ROOT_CA_FILE_PATH "/root_ca.pem"

// Maximum accepted PEM cert size (single root cert is ~2KB; allow headroom).
#define ROOT_CA_MAX_SIZE 8192

// Compile-time default trust bundle used when no override is loaded from
// LittleFS. Contains multiple PEM blocks; mbedTLS treats each as a separate
// trust anchor. In order: ISRG Root X1 (Let's Encrypt RSA), ISRG Root X2
// (Let's Encrypt ECDSA), and Go Daddy Root CA G2 (legacy/transition fallback).
extern const char *DEFAULT_ROOT_CA_CERT;

// Loads the cert override from LittleFS into the in-memory cache, if present
// and valid. Falls back to DEFAULT_ROOT_CA_CERT otherwise. Safe to call
// multiple times; subsequent calls re-read the file.
void initCertManagement();

// Returns the active root CA cert (PEM, NUL-terminated). Never returns null:
// if no override is loaded, returns DEFAULT_ROOT_CA_CERT. Pointer is owned by
// this module and remains valid until the next updateRootCACert() call.
const char *getRootCACert();

// Validates the given PEM cert and, on success, writes it to LittleFS and
// updates the in-memory cache. Returns true on success.
// Validation is structural only: requires BEGIN/END markers and sane length.
bool updateRootCACert(const String &pemCert);

// Removes the override file and reverts to the embedded default.
bool resetRootCACert();

#endif  // CERT_MANAGEMENT_H
