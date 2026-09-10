/* SPDX-License-Identifier: MIT */
#ifndef __NVIDIA_DRM_LINK_RECOVERY_H__
#define __NVIDIA_DRM_LINK_RECOVERY_H__

struct nv_drm_connector;
struct work_struct;

void nv_drm_queue_link_recovery(struct nv_drm_connector *nv_connector);
void nv_drm_handle_link_recovery_work(struct work_struct *work);

#endif
