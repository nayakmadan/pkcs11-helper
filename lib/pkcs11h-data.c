/*
 * Copyright (c) 2005-2006 Alon Bar-Lev <alon.barlev@gmail.com>
 * All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, or the OpenIB.org BSD license.
 *
 * GNU General Public License (GPL) Version 2
 * ===========================================
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING[.GPL2] included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * OpenIB.org BSD license
 * =======================
 * Redistribution and use in source and binary forms, with or without modifi-
 * cation, are permitted provided that the following conditions are met:
 *
 *   o  Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   o  Redistributions in binary form must reproduce the above copyright no-
 *      tice, this list of conditions and the following disclaimer in the do-
 *      cumentation and/or other materials provided with the distribution.
 *
 *   o  The names of the contributors may not be used to endorse or promote
 *      products derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LI-
 * ABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUEN-
 * TIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEV-
 * ER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABI-
 * LITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "common.h"

#include <pkcs11-helper-1.0/pkcs11h-data.h>
#include "_pkcs11h-mem.h"
#include "_pkcs11h-session.h"

#if defined(ENABLE_PKCS11H_DATA)

static
CK_RV
_pkcs11h_data_getObject (
	IN const pkcs11h_session_t session,
	IN const char * const application,
	IN const char * const label,
	OUT CK_OBJECT_HANDLE * const p_handle
) {
	CK_OBJECT_CLASS class = CKO_DATA;
	CK_ATTRIBUTE filter[] = {
		{CKA_CLASS, (void *)&class, sizeof (class)},
		{CKA_APPLICATION, (void *)application, application == NULL ? 0 : strlen (application)},
		{CKA_LABEL, (void *)label, label == NULL ? 0 : strlen (label)}
	};
	CK_OBJECT_HANDLE *objects = NULL;
	CK_ULONG objects_found = 0;
	CK_RV rv = CKR_OK;
	
	PKCS11H_ASSERT (session!=NULL);
	PKCS11H_ASSERT (application!=NULL);
	PKCS11H_ASSERT (label!=NULL);

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: _pkcs11h_data_getObject entry session=%p, application='%s', label='%s', p_handle=%p",
		(void *)session,
		application,
		label,
		(void *)p_handle
	);

	*p_handle = PKCS11H_INVALID_OBJECT_HANDLE;

	if (rv == CKR_OK) {
		rv = _pkcs11h_session_validate (session);
	}

	if (rv == CKR_OK) {
		rv = _pkcs11h_session_findObjects (
			session,
			filter,
			sizeof (filter) / sizeof (CK_ATTRIBUTE),
			&objects,
			&objects_found
		);
	}

	if (
		rv == CKR_OK &&
		objects_found == 0
	) {
		rv = CKR_FUNCTION_REJECTED;
	}

	if (rv == CKR_OK) {
		*p_handle = objects[0];
	}

	if (objects != NULL) {
		_pkcs11h_mem_free ((void *)&objects);
	}

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: _pkcs11h_data_getObject return rv=%ld-'%s', *p_handle=%08lx",
		rv,
		pkcs11h_getMessage (rv),
		(unsigned long)*p_handle
	);

	return rv;
}

CK_RV
pkcs11h_data_get (
	IN const pkcs11h_token_id_t token_id,
	IN const PKCS11H_BOOL is_public,
	IN const char * const application,
	IN const char * const label,
	IN void * const user_data,
	IN const unsigned mask_prompt,
	OUT unsigned char * const blob,
	IN OUT size_t * const p_blob_size
) {
	CK_ATTRIBUTE attrs[] = {
		{CKA_VALUE, NULL, 0}
	};
	CK_OBJECT_HANDLE handle = PKCS11H_INVALID_OBJECT_HANDLE;
	CK_RV rv = CKR_OK;

#if defined(ENABLE_PKCS11H_THREADING)
	PKCS11H_BOOL mutex_locked = FALSE;
#endif
	pkcs11h_session_t session = NULL;
	PKCS11H_BOOL op_succeed = FALSE;
	PKCS11H_BOOL login_retry = FALSE;
	size_t blob_size_max = 0;

	PKCS11H_ASSERT (g_pkcs11h_data!=NULL);
	PKCS11H_ASSERT (g_pkcs11h_data->initialized);
	PKCS11H_ASSERT (token_id!=NULL);
	PKCS11H_ASSERT (application!=NULL);
	PKCS11H_ASSERT (label!=NULL);
	/*PKCS11H_ASSERT (user_data) NOT NEEDED */
	/*PKCS11H_ASSERT (blob!=NULL); NOT NEEDED*/
	PKCS11H_ASSERT (p_blob_size!=NULL);

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_get entry token_id=%p, application='%s', label='%s', user_data=%p, mask_prompt=%08x, blob=%p, *p_blob_size=%u",
		(void *)token_id,
		application,
		label,
		user_data,
		mask_prompt,
		blob,
		blob != NULL ? *p_blob_size : 0
	);

	if (blob != NULL) {
		blob_size_max = *p_blob_size;
	}
	*p_blob_size = 0;

	if (rv == CKR_OK) {
		rv = _pkcs11h_session_getSessionByTokenId (
			token_id,
			&session
		);
	}

#if defined(ENABLE_PKCS11H_THREADING)
	if (
		rv == CKR_OK &&
		(rv = _pkcs11h_threading_mutexLock (&session->mutex)) == CKR_OK
	) {
		mutex_locked = TRUE;
	}
#endif

	while (rv == CKR_OK && !op_succeed) {

		if (rv == CKR_OK) {
			rv = _pkcs11h_session_validate (session);
		}

		if (rv == CKR_OK) {
			rv = _pkcs11h_data_getObject (
				session,
				application,
				label,
				&handle
			);
		}

		if (rv == CKR_OK) {
			rv = _pkcs11h_session_getObjectAttributes (
				session,
				handle,
				attrs,
				sizeof (attrs)/sizeof (CK_ATTRIBUTE)
			);
		}

		if (rv == CKR_OK) {
			op_succeed = TRUE;
		}
		else {
			if (!login_retry) {
				PKCS11H_DEBUG (
					PKCS11H_LOG_DEBUG1,
					"PKCS#11: Read data object failed rv=%ld-'%s'",
					rv,
					pkcs11h_getMessage (rv)
				);
				login_retry = TRUE;
				rv = _pkcs11h_session_login (
					session,
					is_public,
					TRUE,
					user_data,
					mask_prompt
				);
			}
		}
	}

#if defined(ENABLE_PKCS11H_THREADING)
	if (mutex_locked) {
		_pkcs11h_threading_mutexRelease (&session->mutex);
		mutex_locked = FALSE;
	}
#endif

	if (rv == CKR_OK) {
		*p_blob_size = attrs[0].ulValueLen;
	}

	if (rv == CKR_OK) {
		if (blob != NULL) {
			if (*p_blob_size > blob_size_max) {
				rv = CKR_BUFFER_TOO_SMALL;
			}
			else {
				memmove (blob, attrs[0].pValue, *p_blob_size);
			}
		}
	}

	_pkcs11h_session_freeObjectAttributes (
		attrs,
		sizeof (attrs)/sizeof (CK_ATTRIBUTE)
	);

	if (session != NULL) {
		_pkcs11h_session_release (session);
		session = NULL;
	}

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_get return rv=%ld-'%s', *p_blob_size=%u",
		rv,
		pkcs11h_getMessage (rv),
		*p_blob_size
	);

	return rv;
}

CK_RV
pkcs11h_data_put (
	IN const pkcs11h_token_id_t token_id,
	IN const PKCS11H_BOOL is_public,
	IN const char * const application,
	IN const char * const label,
	IN void * const user_data,
	IN const unsigned mask_prompt,
	OUT unsigned char * const blob,
	IN const size_t blob_size
) {
	CK_OBJECT_CLASS class = CKO_DATA;
	CK_BBOOL ck_true = CK_TRUE;
	CK_BBOOL ck_false = CK_FALSE;

	CK_ATTRIBUTE attrs[] = {
		{CKA_CLASS, &class, sizeof (class)},
		{CKA_TOKEN, &ck_true, sizeof (ck_true)},
		{CKA_PRIVATE, is_public ? &ck_false : &ck_true, sizeof (CK_BBOOL)},
		{CKA_APPLICATION, (void *)application, strlen (application)},
		{CKA_LABEL, (void *)label, strlen (label)},
		{CKA_VALUE, blob, blob_size}
	};

	CK_OBJECT_HANDLE handle = PKCS11H_INVALID_OBJECT_HANDLE;
	CK_RV rv = CKR_OK;

#if defined(ENABLE_PKCS11H_THREADING)
	PKCS11H_BOOL mutex_locked = FALSE;
#endif
	pkcs11h_session_t session = NULL;
	PKCS11H_BOOL op_succeed = FALSE;
	PKCS11H_BOOL login_retry = FALSE;

	PKCS11H_ASSERT (g_pkcs11h_data!=NULL);
	PKCS11H_ASSERT (g_pkcs11h_data->initialized);
	PKCS11H_ASSERT (token_id!=NULL);
	PKCS11H_ASSERT (application!=NULL);
	PKCS11H_ASSERT (label!=NULL);
	/*PKCS11H_ASSERT (user_data) NOT NEEDED */
	PKCS11H_ASSERT (blob!=NULL);

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_put entry token_id=%p, application='%s', label='%s', user_data=%p, mask_prompt=%08x, blob=%p, blob_size=%u",
		(void *)token_id,
		application,
		label,
		user_data,
		mask_prompt,
		blob,
		blob != NULL ? blob_size : 0
	);

	if (rv == CKR_OK) {
		rv = _pkcs11h_session_getSessionByTokenId (
			token_id,
			&session
		);
	}

#if defined(ENABLE_PKCS11H_THREADING)
	if (
		rv == CKR_OK &&
		(rv = _pkcs11h_threading_mutexLock (&session->mutex)) == CKR_OK
	) {
		mutex_locked = TRUE;
	}
#endif

	while (rv == CKR_OK && !op_succeed) {

		if (rv == CKR_OK) {
			rv = _pkcs11h_session_validate (session);
		}

		if (rv == CKR_OK) {
			rv = session->provider->f->C_CreateObject (
				session->session_handle,
				attrs,
				sizeof (attrs)/sizeof (CK_ATTRIBUTE),
				&handle
			);
		}

		if (rv == CKR_OK) {
			op_succeed = TRUE;
		}
		else {
			if (!login_retry) {
				PKCS11H_DEBUG (
					PKCS11H_LOG_DEBUG1,
					"PKCS#11: Write data object failed rv=%ld-'%s'",
					rv,
					pkcs11h_getMessage (rv)
				);
				login_retry = TRUE;
				rv = _pkcs11h_session_login (
					session,
					is_public,
					FALSE,
					user_data,
					mask_prompt
				);
			}
		}
	}

#if defined(ENABLE_PKCS11H_THREADING)
	if (mutex_locked) {
		_pkcs11h_threading_mutexRelease (&session->mutex);
		mutex_locked = FALSE;
	}
#endif

	if (session != NULL) {
		_pkcs11h_session_release (session);
		session = NULL;
	}

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_put return rv=%ld-'%s'",
		rv,
		pkcs11h_getMessage (rv)
	);

	return rv;
}

CK_RV
pkcs11h_data_del (
	IN const pkcs11h_token_id_t token_id,
	IN const PKCS11H_BOOL is_public,
	IN const char * const application,
	IN const char * const label,
	IN void * const user_data,
	IN const unsigned mask_prompt
) {
#if defined(ENABLE_PKCS11H_THREADING)
	PKCS11H_BOOL mutex_locked = FALSE;
#endif
	pkcs11h_session_t session = NULL;
	PKCS11H_BOOL op_succeed = FALSE;
	PKCS11H_BOOL login_retry = FALSE;
	CK_OBJECT_HANDLE handle = PKCS11H_INVALID_OBJECT_HANDLE;
	CK_RV rv = CKR_OK;

	PKCS11H_ASSERT (g_pkcs11h_data!=NULL);
	PKCS11H_ASSERT (g_pkcs11h_data->initialized);
	PKCS11H_ASSERT (token_id!=NULL);
	PKCS11H_ASSERT (application!=NULL);
	PKCS11H_ASSERT (label!=NULL);
	/*PKCS11H_ASSERT (user_data) NOT NEEDED */

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_del entry token_id=%p, application='%s', label='%s', user_data=%p, mask_prompt=%08x",
		(void *)token_id,
		application,
		label,
		user_data,
		mask_prompt
	);

	if (rv == CKR_OK) {
		rv = _pkcs11h_session_getSessionByTokenId (
			token_id,
			&session
		);
	}

#if defined(ENABLE_PKCS11H_THREADING)
	if (
		rv == CKR_OK &&
		(rv = _pkcs11h_threading_mutexLock (&session->mutex)) == CKR_OK
	) {
		mutex_locked = TRUE;
	}
#endif

	while (rv == CKR_OK && !op_succeed) {

		if (rv == CKR_OK) {
			rv = _pkcs11h_session_validate (session);
		}

		if (rv == CKR_OK) {
			rv = _pkcs11h_data_getObject (
				session,
				application,
				label,
				&handle
			);
		}

		if (rv == CKR_OK) {
			rv = session->provider->f->C_DestroyObject (
				session->session_handle,
				handle
			);
		}

		if (rv == CKR_OK) {
			op_succeed = TRUE;
		}
		else {
			if (!login_retry) {
				PKCS11H_DEBUG (
					PKCS11H_LOG_DEBUG1,
					"PKCS#11: Remove data object failed rv=%ld-'%s'",
					rv,
					pkcs11h_getMessage (rv)
				);
				login_retry = TRUE;
				rv = _pkcs11h_session_login (
					session,
					is_public,
					FALSE,
					user_data,
					mask_prompt
				);
			}
		}
	}

#if defined(ENABLE_PKCS11H_THREADING)
		if (mutex_locked) {
			_pkcs11h_threading_mutexRelease (&session->mutex);
			mutex_locked = FALSE;
		}
#endif

	if (session != NULL) {
		_pkcs11h_session_release (session);
		session = NULL;
	}

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_del return rv=%ld-'%s'",
		rv,
		pkcs11h_getMessage (rv)
	);

	return rv;
}

CK_RV
pkcs11h_data_freeDataIdList (
	IN const pkcs11h_data_id_list_t data_id_list
) {
	pkcs11h_data_id_list_t _id = data_id_list;

	PKCS11H_ASSERT (g_pkcs11h_data!=NULL);
	PKCS11H_ASSERT (g_pkcs11h_data->initialized);
	/*PKCS11H_ASSERT (data_id_list!=NULL); NOT NEEDED*/

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_freeDataIdList entry token_id_list=%p",
		(void *)data_id_list
	);

	while (_id != NULL) {
		pkcs11h_data_id_list_t x = _id;
		_id = _id->next;

		if (x->application != NULL) {
			_pkcs11h_mem_free ((void *)&x->application);
		}
		if (x->label != NULL) {
			_pkcs11h_mem_free ((void *)&x->label);
		}
		_pkcs11h_mem_free ((void *)&x);
	}

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_token_freeDataIdList return"
	);

	return CKR_OK;
}

CK_RV
pkcs11h_data_enumDataObjects (
	IN const pkcs11h_token_id_t token_id,
	IN const PKCS11H_BOOL is_public,
	IN void * const user_data,
	IN const unsigned mask_prompt,
	OUT pkcs11h_data_id_list_t * const p_data_id_list
) {
#if defined(ENABLE_PKCS11H_THREADING)
	PKCS11H_BOOL mutex_locked = FALSE;
#endif
	pkcs11h_session_t session = NULL;
	pkcs11h_data_id_list_t data_id_list = NULL;
	CK_RV rv = CKR_OK;

	PKCS11H_BOOL op_succeed = FALSE;
	PKCS11H_BOOL login_retry = FALSE;

	PKCS11H_ASSERT (g_pkcs11h_data!=NULL);
	PKCS11H_ASSERT (g_pkcs11h_data->initialized);
	PKCS11H_ASSERT (p_data_id_list!=NULL);

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_enumDataObjects entry token_id=%p, is_public=%d, user_data=%p, mask_prompt=%08x, p_data_id_list=%p",
		(void *)token_id,
		is_public ? 1 : 0,
		user_data,
		mask_prompt,
		(void *)p_data_id_list
	);

	*p_data_id_list = NULL;

	if (rv == CKR_OK) {
		rv = _pkcs11h_session_getSessionByTokenId (
			token_id,
			&session
		);
	}

#if defined(ENABLE_PKCS11H_THREADING)
	if (
		rv == CKR_OK &&
		(rv = _pkcs11h_threading_mutexLock (&session->mutex)) == CKR_OK
	) {
		mutex_locked = TRUE;
	}
#endif

	while (rv == CKR_OK && !op_succeed) {

		CK_OBJECT_CLASS class = CKO_DATA;
		CK_ATTRIBUTE filter[] = {
			{CKA_CLASS, (void *)&class, sizeof (class)}
		};
		CK_OBJECT_HANDLE *objects = NULL;
		CK_ULONG objects_found = 0;

		CK_ULONG i;

		if (rv == CKR_OK) {
			rv = _pkcs11h_session_validate (session);
		}

		if (rv == CKR_OK) {
			rv = _pkcs11h_session_findObjects (
				session,
				filter,
				sizeof (filter) / sizeof (CK_ATTRIBUTE),
				&objects,
				&objects_found
			);
		}

		for (i = 0;rv == CKR_OK && i < objects_found;i++) {
			pkcs11h_data_id_list_t entry = NULL;

			CK_ATTRIBUTE attrs[] = {
				{CKA_APPLICATION, NULL, 0},
				{CKA_LABEL, NULL, 0}
			};

			if (rv == CKR_OK) {
				rv = _pkcs11h_session_getObjectAttributes (
					session,
					objects[i],
					attrs,
					sizeof (attrs) / sizeof (CK_ATTRIBUTE)
				);
			}
			
			if (rv == CKR_OK) {
				rv = _pkcs11h_mem_malloc (
					(void *)&entry,
					sizeof (struct pkcs11h_data_id_list_s)
				);
			}

			if (
				rv == CKR_OK &&
				(rv = _pkcs11h_mem_malloc (
					(void *)&entry->application,
					attrs[0].ulValueLen+1
				)) == CKR_OK
			) {
				memmove (entry->application, attrs[0].pValue, attrs[0].ulValueLen);
				entry->application[attrs[0].ulValueLen] = '\0';
			}

			if (
				rv == CKR_OK &&
				(rv = _pkcs11h_mem_malloc (
					(void *)&entry->label,
					attrs[1].ulValueLen+1
				)) == CKR_OK
			) {
				memmove (entry->label, attrs[1].pValue, attrs[1].ulValueLen);
				entry->label[attrs[1].ulValueLen] = '\0';
			}

			if (rv == CKR_OK) {
				entry->next = data_id_list;
				data_id_list = entry;
				entry = NULL;
			}

			_pkcs11h_session_freeObjectAttributes (
				attrs,
				sizeof (attrs) / sizeof (CK_ATTRIBUTE)
			);

			if (entry != NULL) {
				if (entry->application != NULL) {
					_pkcs11h_mem_free ((void *)&entry->application);
				}
				if (entry->label != NULL) {
					_pkcs11h_mem_free ((void *)&entry->label);
				}
				_pkcs11h_mem_free ((void *)&entry);
			}
		}

		if (objects != NULL) {
			_pkcs11h_mem_free ((void *)&objects);
		}

		if (rv == CKR_OK) {
			op_succeed = TRUE;
		}
		else {
			if (!login_retry) {
				PKCS11H_DEBUG (
					PKCS11H_LOG_DEBUG1,
					"PKCS#11: Enumerate data objects failed rv=%ld-'%s'",
					rv,
					pkcs11h_getMessage (rv)
				);
				login_retry = TRUE;
				rv = _pkcs11h_session_login (
					session,
					is_public,
					TRUE,
					user_data,
					mask_prompt
				);
			}
		}
	}

#if defined(ENABLE_PKCS11H_THREADING)
	if (mutex_locked) {
		_pkcs11h_threading_mutexRelease (&session->mutex);
		mutex_locked = FALSE;
	}
#endif

	if (rv == CKR_OK) {
		*p_data_id_list = data_id_list;
		data_id_list = NULL;
	}

	if (session != NULL) {
		_pkcs11h_session_release (session);
		session = NULL;
	}

	if (data_id_list != NULL) {
		pkcs11h_data_freeDataIdList (data_id_list);
		data_id_list = NULL;
	}

	PKCS11H_DEBUG (
		PKCS11H_LOG_DEBUG2,
		"PKCS#11: pkcs11h_data_enumDataObjects return rv=%ld-'%s', *p_data_id_list=%p",
		rv,
		pkcs11h_getMessage (rv),
		(void *)*p_data_id_list
	);
	
	return rv;
}

#endif				/* ENABLE_PKCS11H_DATA */