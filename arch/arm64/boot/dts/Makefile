# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_MACH_EXYNOS7880_A5Y17LTE_EUR_OPEN) := \
	exynos7880-a5y17lte_eur_open_00.dtb \
	exynos7880-a5y17lte_eur_open_01.dtb \
	exynos7880-a5y17lte_eur_open_02.dtb \
	exynos7880-a5y17lte_eur_open_03.dtb \
	exynos7880-a5y17lte_eur_open_05.dtb \
	exynos7880-a5y17lte_eur_open_07.dtb \
	exynos7880-a5y17lte_eur_open_08.dtb
dtb-$(CONFIG_MACH_EXYNOS7880_A7Y17LTE_EUR_OPEN) := \
	exynos7880-a7y17lte_eur_open_00.dtb \
	exynos7880-a7y17lte_eur_open_01.dtb \
	exynos7880-a7y17lte_eur_open_02.dtb \
	exynos7880-a7y17lte_eur_open_03.dtb \
	exynos7880-a7y17lte_eur_open_04.dtb \
	exynos7880-a7y17lte_eur_open_06.dtb

targets += dtbs

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
targets += $(DTB_LIST)
dtbs: $(addprefix $(obj)/, $(DTB_LIST))
clean-files := dts/*.dtb *.dtb
