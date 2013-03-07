/*
 *  Copyright (C) 2013 LSI Corporation
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "ai2c_sal.h"

/*
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 */

/*
 * definitions of the ai2c_malloc/ai2c_nvm_malloc family of functions.
 */

void *ai2c_malloc(size_t size)
{
	void *p;

	if (size <= 0) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING: ai2c_malloc(%d) passed a zero or "
			"less size.\n",
			size);
#endif
	return 0;
	}

	p = __ai2c_malloc(size);
	if (p == NULL)
		AI2C_MSG(AI2C_MSG_ERROR, "ai2c_malloc(%d) failed.\n", size);

	return p;
}

void *ai2c_calloc(size_t no, size_t size)
{
	void *p;

	if (size <= 0 || no <= 0) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING: ai2c_calloc(no=%d, size=%d) "
			"passed a zero or less size.\n",
			no, size);
#endif
		return 0;
	}

	p = __ai2c_calloc(no, size);
	if (p == NULL) {
		AI2C_MSG(AI2C_MSG_ERROR,
			"ai2c_calloc(no=%d, size=%d) failed.\n", no, size);
	}
	return p;
}

void *ai2c_realloc(void *ptr, size_t size)
{
	if (size <= 0) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING: ai2c_realloc(%d) passed a zero or "
			"less size.\n",
			size);
#endif
		return 0;
	}

	ptr = __ai2c_realloc(ptr, size);
	if (ptr == NULL) {
		AI2C_MSG(AI2C_MSG_ERROR,
			"ai2c_realloc(ptr=%p, size=%d) failed.\n",
			ptr, size);
	}
	return ptr;
}

void ai2c_free(void *ptr)
{
	if (ptr == NULL) {
#ifdef AI2C_DEBUG
		AI2C_MSG(AI2C_MSG_DEBUG,
			"WARNING:  ai2c_free(%p) passed a NULL pointer.\n",
			ptr);
#endif
		return;
	}

	__ai2c_free(ptr);
}
