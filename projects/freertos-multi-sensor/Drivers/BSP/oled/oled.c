#include "oled.h"
#include "delay.h"
#include "font.h"

//使用软件模拟 IIC, 所以要初始化 GPIO
void oled_gpio_init(void)
{
	GPIO_InitTypeDef gpio_initstruct;
	//打开时钟
	OLED_I2C_SCL_CLK();
	OLED_I2C_SDA_CLK();
	
	//初始化 CLK
	gpio_initstruct.Pin = OLED_I2C_SCL_PIN; //SCL 引脚
	gpio_initstruct.Mode = GPIO_MODE_OUTPUT_PP; //推挽输出, 我们SCL 引脚没有接上拉电阻，开漏输出只能输出低电平，不行
	gpio_initstruct.Pull = GPIO_PULLUP; //上拉
	gpio_initstruct.Speed = GPIO_SPEED_FREQ_HIGH; //高速
	HAL_GPIO_Init(OLED_I2C_SCL_PORT, &gpio_initstruct);
	
	gpio_initstruct.Pin = OLED_I2C_SDA_PIN; //SDA 引脚
	//复用上面的结构体变量初始化
	HAL_GPIO_Init(OLED_I2C_SDA_PORT, &gpio_initstruct);
}

//IIC 相关内容封装
//start 信号
void oled_i2c_start(void)
{
	//一开始 SCL 和 SDA 都是高电平状态
	OLED_SCL_SET();
	OLED_SDA_SET();
	
	//接下来把 SDA 拉低
	OLED_SDA_RESET();
	//SCL 也变成低电平
	OLED_SCL_RESET();
}

//stop 信号
void oled_i2c_stop(void)
{
	//SCL 是高电平状态
	OLED_SCL_SET();
	//SDA 由低电平变为高电平
	OLED_SDA_RESET();
	OLED_SDA_SET();
}

//ack 应答信号
void oled_i2c_ack(void)
{
	//在 SCL 高电平时读取 SDA 的 ACK 信号
	//我们这里不需要做处理，所以只需要 SCL 从高到低即可
	OLED_SCL_SET();
	OLED_SCL_RESET();
}

//i2c 写一个字节
void oled_i2c_write_byte(uint8_t data)
{
	//循环 8 次就发送一个字节
	uint8_t i, tmp;
	tmp = data; //不会修改原来的数据
	for(i = 0; i < 8; i++)
	{
		//高位先行
		if((tmp & 0x80) == 0x80) //取出的最高位为 1
			//SDA 拉高
			OLED_SDA_SET();
		else
			//否则为0 SDA 拉低
			OLED_SDA_RESET();
		//然后 tmp 再左移 1 位来看次高位，以此类推
		tmp = tmp << 1;
		OLED_SCL_SET(); //SCL 高电平时从机读取数据位
		OLED_SCL_RESET(); //SCL 再低电平，位下次写数据做准备
	}
}

//发送一个命令
void oled_write_cmd(uint8_t cmd)
{
	//1. 起始信号
	oled_i2c_start();
	//2. 从机地址，之前讲过是 0x78
	oled_i2c_write_byte(0x78);
	//3. ack
	oled_i2c_ack();
	//4. Control byte
	oled_i2c_write_byte(0x00);
	//5. ack
	oled_i2c_ack();
	//6. cmd
	oled_i2c_write_byte(cmd);
	//7. ack
	oled_i2c_ack();
	//8. 停止信号
	oled_i2c_stop();
}

//发送一个数据
void oled_write_data(uint8_t data)
{
	//1. 起始信号
	oled_i2c_start();
	//2. 从机地址，之前讲过是 0x78
	oled_i2c_write_byte(0x78);
	//3. ack
	oled_i2c_ack();
	//4. Control byte
	oled_i2c_write_byte(0x40);
	//5. ack
	oled_i2c_ack();
	//6. cmd
	oled_i2c_write_byte(data);
	//7. ack
	oled_i2c_ack();
	//8. 停止信号
	oled_i2c_stop();
}

//OLED 初始化
void oled_init(void)
{
	oled_gpio_init();
	
	delay_ms(100); //让 OLED 做好准备
	
	oled_write_cmd(0xAE);    //设置显示开启/关闭，0xAE关闭，0xAF开启

	oled_write_cmd(0xD5);    //设置显示时钟分频比/振荡器频率
	oled_write_cmd(0x80);    //0x00~0xFF

	oled_write_cmd(0xA8);    //设置多路复用率
	oled_write_cmd(0x3F);    //0x0E~0x3F

	oled_write_cmd(0xD3);    //设置显示偏移
	oled_write_cmd(0x00);    //0x00~0x7F

	oled_write_cmd(0x40);    //设置显示开始行，0x40~0x7F

	oled_write_cmd(0xA1);    //设置左右方向，0xA1正常，0xA0左右反置

	oled_write_cmd(0xC8);    //设置上下方向，0xC8正常，0xC0上下反置

	oled_write_cmd(0xDA);    //设置COM引脚硬件配置
	oled_write_cmd(0x12);

	oled_write_cmd(0x81);    //设置对比度
	oled_write_cmd(0xCF);    //0x00~0xFF

	oled_write_cmd(0xD9);    //设置预充电周期
	oled_write_cmd(0xF1);

	oled_write_cmd(0xDB);    //设置VCOMH取消选择级别
	oled_write_cmd(0x30);

	oled_write_cmd(0xA4);    //设置整个显示打开/关闭

	oled_write_cmd(0xA6);    //设置正常/反色显示，0xA6正常，0xA7反色

	oled_write_cmd(0x8D);    //设置充电泵
	oled_write_cmd(0x14);

	oled_write_cmd(0xAF);    //开启显示
}

//指定坐标的函数(x:列(0~127),y: PAGE(0~7))
void oled_set_cursor(uint8_t x, uint8_t y)
{
	oled_write_cmd(0XB0 + y); //PAGEy
	//列
	//取出低 4 位或上 0X00
	oled_write_cmd((x & 0X0F) | 0X00); //先发低位
	//取出高4位并移位到低4位，然后或上 0X10
	oled_write_cmd(((x & 0XF0) >> 4) | 0X10); //再发高位
}

//清屏函数
void oled_fill(uint8_t data)
{
	//每个 PAGE，每个列都赋一个值
	uint8_t i, j;
	for(i = 0; i < 8; i++) //外层循环控制行 PAGE
	{
		oled_set_cursor(0, i); //默认的页地址模式列地址会随着写入数据自动递增，下面就不用写列的坐标
		for(j = 0; j < 128; j++) //内层循环控制列 COL
		{
			oled_write_data(data);
		}
	}
}

//显示字符函数
//x: 列， y：PAGE，num：哪个字符，size：大小，字符高度
void oled_show_char(uint8_t x, uint8_t y, uint8_t num, uint8_t size)
{
	uint8_t i, j, page;
	
	num = num - ' '; //减去空格
	
	page = size / 8;
	//比如 size = 10，就有余数，就应该 2 个 page，而上面的只能取到整数1，要 +1
	if(size % 8)
		page++;
	
	//显示字符
	for(j = 0; j < page; j++) //外循环 PAGE
	{
		oled_set_cursor(x, y + j);
		for(i = size / 2 * j; i < size /2 * (j + 1); i++) //列
		{
			if(size == 12)
				oled_write_data(ascii_6X12[num][i]); //数组中的num行，列的位置 i
			if(size == 16)
				oled_write_data(ascii_8X16[num][i]); //数组中的num行，列的位置 i
			if(size == 24)
				oled_write_data(ascii_12X24[num][i]); //数组中的num行，列的位置 i
		}
	}
}

//显示字符串
//x：列，y：PAGE，p：字符串，size：大小，字符高度
void oled_show_string(uint8_t x, uint8_t y, char *p, uint8_t size)
{
	while(*p != '\0')
	{
		//显示一个字符
		oled_show_char(x, y, *p, size);
		//偏移一个字符，字符的宽度等于高度的一半
		x += size/2;
		p++;
	}
}

//显示一个汉字
//x：列，y：PAGE，num：哪个汉字，size：大小，汉字有多高
void oled_show_chinese(uint8_t x, uint8_t y, uint8_t num, uint8_t size)
{
	uint16_t i, j;
	//PAGE 多少呢？汉字我们一般设为 8 的倍数
	for(j = 0; j < size/8; j++) //PAGE
	{
		oled_set_cursor(x, y + j);
		//汉字宽度和高度一样
		for(i = size * j; i < size * (j + 1); i++) //列
		{
			if(size == 16)
				oled_write_data(chinese_16x16[num][i]);
			else if(size == 24)
				oled_write_data(chinese_24x24[num][i]);
		}
	}
}

//显示图片
//x：列，y：PAGE，width：图片宽度，height：图片高度，bmp：图片数据
void oled_show_img(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t *bmp)
{
	uint8_t i,j ; //提取图片每一个元素
	for(j = 0; j < height/8; j++)
	{
		oled_set_cursor(x, y + j);
		for(i = 0; i < width; i++) //列
		{
			oled_write_data(bmp[width * j + i]);
		}
	}
}

