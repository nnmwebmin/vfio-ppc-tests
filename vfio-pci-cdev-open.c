
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <stdint.h>

#include <linux/iommufd.h>
#include <linux/vfio.h>
#include <linux/ioctl.h>
#include <errno.h>


void usage(char *name)
{
	printf("usage: %s <iommu group id> <ssss:bb:dd.f>\n", name);
}

int main(int argc, char **argv)
{
	int i, ret, container, group, device, groupid, cdev_fd, iommufd;
	char path[PATH_MAX];
	int seg, bus, dev, func;
	unsigned long config_offset;
	char  vid[9];

	struct vfio_group_status group_status;
	struct vfio_device_info device_info = { 
		.argsz = sizeof(device_info) };


	struct vfio_region_info region_info = {
		.argsz = sizeof(region_info)
	};
	struct vfio_iommu_type1_info info_cmd = {};

	struct vfio_device_bind_iommufd bind = {
        .argsz = sizeof(bind),
        .flags = 0,
};
struct iommu_ioas_alloc alloc_data  = {
        .size = sizeof(alloc_data),
        .flags = 0,
};
struct vfio_device_attach_iommufd_pt attach_data = {
        .argsz = sizeof(attach_data),
        .flags = 0,
};
struct iommu_ioas_map map = {
        .size = sizeof(map),
        .flags = IOMMU_IOAS_MAP_READABLE |
                 IOMMU_IOAS_MAP_WRITEABLE |
                 IOMMU_IOAS_MAP_FIXED_IOVA,
        .__reserved = 0,
};
 	struct vfio_iommu_spapr_register_memory reg = {          
                .argsz = sizeof(reg),                                           
                .flags = 0                                                      
        }; 


int buf_size = ( (sizeof(struct vfio_device_feature)+ sizeof(uint64_t) -1 ) /sizeof(uint64_t));
printf("buf size %d \n",buf_size);
uint64_t buf[buf_size] = {};


	if (argc < 3) {
		usage(argv[0]);
		return -1;
	}

	ret = sscanf(argv[1], "%d", &groupid);
	if (ret != 1) {
		usage(argv[0]);
		return -1;
	}

	ret = sscanf(argv[2], "%04x:%02x:%02x.%d", &seg, &bus, &dev, &func);
	if (ret != 4) {
		usage(argv[0]);
		return -1;
	}

	printf("Using PCI device %04x:%02x:%02x.%d in group %d\n",
               seg, bus, dev, func, groupid);

	container = open("/dev/vfio/vfio", O_RDWR);
	if (container < 0) {
		printf("Failed to open /dev/vfio/vfio, %d (%s)\n",
		       container, strerror(errno));
		return container;
	}

	snprintf(path, sizeof(path), "/dev/vfio/%d", groupid);

	cdev_fd = open("/dev/vfio/devices/vfio0", O_RDWR);
	if (cdev_fd < 0) {
		printf("Failed to open %s, %d (%s)\n",
		       path, group, strerror(errno));
		return cdev_fd;
	}

iommufd = open("/dev/iommu", O_RDWR);
perror("open iommufd ");
bind.iommufd = iommufd;
ret = ioctl(cdev_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind);
perror("VFIO_DEVICE_BIND_IOMMUFD \n");
printf("VFIO_DEVICE_BIND_IOMMUFD return %d \n bind.flags 0x%X, bind.argsz %d, bind.iommufd %d, bind.out_devid 0x%X\n",
	       	ret, bind.flags, bind.argsz, bind.iommufd, bind.out_devid);
ioctl(iommufd, IOMMU_IOAS_ALLOC, &alloc_data);
attach_data.pt_id = alloc_data.out_ioas_id;
ioctl(cdev_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach_data);
perror("VFIO_DEVICE_ATTACH_IOMMUFD_PT");

struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
feature->argsz = sizeof(buf);

 feature->flags = VFIO_DEVICE_FEATURE_SET |
                     VFIO_DEVICE_FEATURE_DMA_LOGGING_STOP;


/* Allocate some space and setup a DMA mapping */
map.user_va = (int64_t)mmap(0, 1024 * 1024, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
map.iova = 0; /* 1MB starting at 0x0 from device view */
map.length = 1024 * 1024;
map.ioas_id = alloc_data.out_ioas_id;


#ifdef __PPC64__
                        reg.vaddr = map.user_va;
                        reg.size = map.length;
                        reg.flags = 0;
                        if (ioctl(iommufd, VFIO_IOMMU_SPAPR_REGISTER_MEMORY, &reg)) {
                                perror("Set iommu register memory failed\n");
                        //        return -1;
                        }
#endif

if(ioctl(iommufd, IOMMU_IOAS_MAP, &map)) {
	perror("IOMMU_IOAS_MAP :");
}
else 
printf("IOMMU_IOAS_MAP success");

getchar();

ret = ioctl(iommufd, VFIO_CHECK_EXTENSION, VFIO_SPAPR_TCE_v2_IOMMU);
printf("VFIO_CHECK_EXTENSION ret =%d \n",ret );

	ret = ioctl(cdev_fd, VFIO_DEVICE_GET_INFO, &device_info);
	if(ret <0) {
		perror("get device info " );
		printf("Failed to get device info %d error no %d\n", ret, errno);
		return -1;
	}

	printf("Device supports %d regions, %d irqs\n",
	       device_info.num_regions, device_info.num_irqs);

	for (i = 0; i < device_info.num_regions; i++) {
		struct vfio_region_info *info;
		printf("Region %d: ", i);
		info = &region_info;
		info->index = i;
		if (ioctl(cdev_fd, VFIO_DEVICE_GET_REGION_INFO, info)) {
			printf("Failed to get info errno = %d\n",errno);
			perror("Failed to get info :");
			continue;
		}

		printf("size 0x%lx, offset 0x%lx, flags 0b%0b\n",
		       (unsigned long)region_info.size,
		       (unsigned long)region_info.offset, region_info.flags);
		if( info->index == VFIO_PCI_CONFIG_REGION_INDEX)
			config_offset = (unsigned long)region_info.offset;

		if (region_info.flags & VFIO_REGION_INFO_FLAG_MMAP) {
			void *map = mmap(NULL, (size_t)region_info.size,
					 PROT_READ, MAP_SHARED, device,
					 (off_t)region_info.offset);
			if (map == MAP_FAILED) {
				printf("mmap failed\n");
				continue;
			}else {
				printf("mapping of region %d success\n", i);
			}

			printf("[");
			fwrite(map, 1, region_info.size > 16 ? 16 :
						region_info.size, stdout);
			printf("]\n");
			munmap(map, (size_t)region_info.size);
		}
	}
pread (cdev_fd, vid,4, config_offset); 
printf("vid %X", *(unsigned long *)vid);

if (ioctl(cdev_fd, VFIO_DEVICE_FEATURE, feature)) {
perror("VFIO_DEVICE_FEATURE ");
}

	printf("Success\n");
	//printf("Press any key to exit\n");
	//fgetc(stdin);

	return 0;
}
