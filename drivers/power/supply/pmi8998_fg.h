
// #define FG_PARAM_MAX 49

/**** Registers *****/

// pmi8998 v2 specific
#define BATT_INFO_CHARGE_MAX_DESIGN 0x4a
#define MEM_INTF_CFG                0x50
#define MEM_INTF_ADDR_LSB           0x61
#define MEM_INTF_RD_DATA0           0x67
#define MEM_INTF_WR_DATA0           0x63

#define SMB2_CABLE_CONNECTED        0x06

// pm8950 / pm89988 common
#define MEM_INTF_IMA_CFG            0x52
#define MEM_INTF_IMA_OPR_STS        0x54
#define MEM_INTF_IMA_EXP_STS        0x55
#define MEM_INTF_IMA_HW_STS         0x56
#define MEM_INTF_BEAT_COUNT         0x57
#define MEM_INTF_IMA_ERR_STS        0x5f
#define MEM_INTF_IMA_BYTE_EN        0x60

#define BATT_INFO_THERM_C1          0x5c
#define BATT_INFO_VBATT_LSB         0xa0
#define BATT_INFO_VBATT_MSB         0xa1
#define BATT_INFO_IBATT_LSB         0xa2
#define BATT_INFO_IBATT_MSB         0xa3
#define BATT_INFO_BATT_TEMP_LSB     0x50
#define BATT_INFO_BATT_TEMP_MSB     0x51
#define BATT_MONOTONIC_SOC          0x09

#define BATT_TEMP_LSB_MASK          GENMASK(7, 0)
#define BATT_TEMP_MSB_MASK          GENMASK(2, 0)

#define REG_BASE(chip)              (chip->base)
#define REG_BATT(chip)              (chip->base + 0x100)
#define REG_MEM(chip)               (chip->base + 0x400)

/* Interrupt offsets */
#define INT_RT_STS                  0x10
#define INT_EN_CLR                  0x16

// Param addresses
#define PARAM_ADDR_BATT_TEMP       0x50
#define PARAM_ADDR_BATT_VOLTAGE    0xa0
#define PARAM_ADDR_BATT_CURRENT    0xa2

enum wa_flags {
	PMI8998_V1_REV_WA,
	PMI8998_V2_REV_WA,
};

enum pmi8998_rev_offsets {
	DIG_MINOR = 0x0,
	DIG_MAJOR = 0x1,
	ANA_MINOR = 0x2,
	ANA_MAJOR = 0x3,
};
enum pmi8998_rev {
	DIG_REV_1 = 0x1,
	DIG_REV_2 = 0x2,
	DIG_REV_3 = 0x3,
};

enum charger_status{
	TRICKLE_CHARGE = 0,
	PRE_CHARGE,
	FAST_CHARGE,
	FULLON_CHARGE,
	TAPER_CHARGE,
	TERMINATE_CHARGE,
	INHIBIT_CHARGE,
	DISABLE_CHARGE,
};

static enum power_supply_property fg_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_TEMP,
	// POWER_SUPPLY_PROP_CHARGE_NOW,
	// POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_STATUS,
};

struct fg_learning_data {
	struct mutex    learning_lock;
	bool            active;
	int64_t         cc_uah;
	int             learned_cc_uah;
	int             init_cc_pc_val;
	int             max_start_soc;
	int             max_increment;
	int             max_decrement;
	int             vbat_est_thr_uv;
	int             max_cap_limit;
	int             min_cap_limit;
	int             min_temp;
	int             max_temp;
};

struct battery_info {
	const char *manufacturer;
	const char *model;
	const char *serial_num;

	int nom_cap_uah;

	int batt_max_voltage_uv_design;
	int batt_max_voltage_uv;
};

struct pmi8998_fg_chip {
	struct device *dev;
	unsigned int base;
	struct regmap *regmap;
	struct mutex lock;

	struct power_supply *bms_psy;

	u8 revision[4];
	bool ima_supported;

	struct battery_info batt_info;

	struct fg_learning_data learning_data;

	int health;
	int status;
};
