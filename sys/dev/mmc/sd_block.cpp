#include "sd_block.h"

#include "sd_card.h"

namespace mmc::sd {

/*
 * block::block
 */
block::block(card *c, ::device *d, off_t size)
: block::device{d, size}
, card_{c}
{ }

/*
 * block::~block
 */
block::~block()
{ }

/*
 * block::v_open
 */
int
block::v_open()
{
	return 0;
}

/*
 * block::v_close
 */
int
block::v_close()
{
	return 0;
}

/*
 * block::v_read
 */
ssize_t
block::v_read(const iovec *iov, size_t iov_off, size_t len, off_t off)
{
	return card_->read(iov, iov_off, len, off);
}

/*
 * block::v_write
 */
ssize_t
block::v_write(const iovec *iov, size_t iov_off, size_t len, off_t off)
{
	return card_->write(iov, iov_off, len, off);
}

/*
 * block::v_ioctl
 */
int
block::v_ioctl(unsigned long cmd, void *arg)
{
	return card_->ioctl(cmd, arg);
}

}
