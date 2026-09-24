/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <string.h>
#include "common/config/config_userapi.h"
#include "common/utils/LOG/log.h"
#include "common/utils/load_module_shlib.h"
#include "srs_est_interface.h"

int load_srs_est_interface(srs_est_interface_t *itf, int max_rx, int max_ports, int max_M, int max_K_TC)
{
  memset(itf, 0, sizeof(*itf));
  if (!config_get_if())
    return -1;
  // opt-in: without an explicit version there is no module to load, keep the fixed-point estimator
  char *version = NULL;
  paramdef_t params[] = {{"shlibversion", NULL, 0, .strptr = &version, .defstrval = NULL, TYPE_STRING, 0, NULL}};
  config_get(config_get_if(), params, sizeofArray(params), LOADER_CONFIG_PREFIX ".srs_est");
  if (!version || !version[0])
    return -1;

  loader_shlibfunc_t fdesc[] = {{.fname = "srs_est_init"}, {.fname = "srs_est_run"}, {.fname = "srs_est_shutdown"}};
  if (load_module_version_shlib("srs_est", version, fdesc, sizeofArray(fdesc), NULL) < 0) {
    LOG_E(PHY, "cannot load SRS estimation module libsrs_est%s.so\n", version);
    return -1;
  }
  itf->init = (srs_est_init_t *)fdesc[0].fptr;
  itf->run = (srs_est_run_t *)fdesc[1].fptr;
  itf->shutdown = (srs_est_shutdown_t *)fdesc[2].fptr;
  strncpy(itf->version, version, sizeof(itf->version) - 1);
  AssertFatal(itf->init(max_rx, max_ports, max_M, max_K_TC) == 0, "cannot initialize SRS estimation module %s\n", version);
  LOG_I(PHY, "SRS channel estimation by module libsrs_est%s.so\n", version);
  return 0;
}

void free_srs_est_interface(srs_est_interface_t *itf)
{
  if (itf->shutdown)
    itf->shutdown();
  memset(itf, 0, sizeof(*itf));
}
