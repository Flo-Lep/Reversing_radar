
#include <stdint.h>
#include <stdbool.h>
#include "macro_types.h"
#include "stm32f1_gpio.h"
#include "reversing_radar.h"
#include "HC-SR04/HCSR04.h"
#include "tft_ili9341/stm32f1_ili9341.h"
#include "stm32f1_extit.h"
#include "ir/ir_receiver.h"


static uint16_t distance;
static int16_t frequency_timer;
static uint32_t t_read_button;
static uint8_t current_mode;
static bool lcd_mode_update;
static bool ir_mode_update;
static volatile uint32_t t = 0;
static volatile uint32_t timer_extit = 0;
static volatile uint32_t timer_lcd_display = 0;
static bool FLAG_IT;
static bool FLAG_DIST_DISPLAY;


typedef enum
	{
		INIT,
		MODE_ALL,
		MODE_SCREEN_ONLY,
		MODE_BUZZER_ONLY,
		MODE_LED_ONLY
	}mode_e;
/*******************PRIVATE FUNCTIONS PROTOTYPES***************************/
	//INIT
	static void US_REVERSING_RADAR_LED_init(void);
	static void US_REVERSING_RADAR_BUZZER_init(void);
	static void US_REVERSING_RADAR_EXTIT_init(void);
	static void US_REVERSING_RADAR_init(void);
	//BACKGROUND FUNCTIONS
	static bool_e button_press_event(void);
	static void US_REVERSING_RADAR_ir_change_mode(void);
	//PROCESS MAIN FUNCTIONS
	static void US_REVERSING_RADAR_mode_management(void);
	static void US_REVERSING_RADAR_lcd_display(void);
	static void US_REVERSING_RADAR_compute_frequency(void);
	static void US_REVERSING_RADAR_manage_peripherals(void);

/*********************INIT FUNCTIONS*************************************/

/*
 * @brief Fonction initialisant la led sur la broche PA10 (GPIO_PIN_10)
 */
void US_REVERSING_RADAR_LED_init(void)
{
	BSP_GPIO_PinCfg(GPIOA,GPIO_PIN_10,GPIO_MODE_OUTPUT_PP,GPIO_NOPULL,GPIO_SPEED_FREQ_HIGH);
}

 /*
  * @brief Fonction initialisant le buzzer sur la broche PB5 (GPIO_PIN_5)
  */
void US_REVERSING_RADAR_BUZZER_init(void)
{
	BSP_GPIO_PinCfg(GPIOB,GPIO_PIN_5,GPIO_MODE_OUTPUT_PP,GPIO_NOPULL,GPIO_SPEED_FREQ_HIGH);
}

/*
  * @brief Cette fonction initialise et autorise les interruptions externes pour la broche IR_RECEIVER (GPIO_PIN_0)
  */
void US_REVERSING_RADAR_EXTIT_init(void)
{
	BSP_GPIO_PinCfg(IR_RECEIVER_GPIO, IR_RECEIVER_PIN, GPIO_MODE_IT_FALLING,GPIO_PULLUP,GPIO_SPEED_FREQ_HIGH);
	EXTIT_set_callback(&US_REVERSING_RADAR_ir_change_mode, EXTI_gpiopin_to_pin_number(IR_RECEIVER_PIN), TRUE);
	EXTIT_enable(0); //C'est là qu'il faut le mettre ?
}

/*
 * @brief Fonction initialisant les diff�rentes variables ainsi que les p�r�ph�riques n�c�ssaires au radar
 */
void US_REVERSING_RADAR_init(void)
{
	distance = 0;
	frequency_timer = 0;
	t_read_button = 0;
	current_mode = 0;
	lcd_mode_update = TRUE;
	ir_mode_update = FALSE;
	FLAG_IT = FALSE;
	FLAG_DIST_DISPLAY = TRUE;
	//P�r�ph�riques utilis�s
	ILI9341_Init();
	US_REVERSING_RADAR_LED_init();
	US_REVERSING_RADAR_BUZZER_init();
	US_REVERSING_RADAR_EXTIT_init();
}

/****************************BACKGROUND FUNCTIONS*******************************/

/*
 * @brief  Fonction qui sert de compteur pour un d�lai
 * @pre	  "Systick_add_callback_function(&process_ms)" doit �tre appel�e dans le main avant le while(1) si l'on souhaite que cette fonction soit appelee toutes les ms
 * @info   Le timer d'affichage agit sur la fréquence du buzzer et de la led... (pourquoi ?)
 */
uint16_t process_ms(void)
{
	if(t_read_button)  //Compteur pour l'appui sur un bouton
		t_read_button--;
	if(t)
		t --;
	if(!timer_extit) //Compteur pour les it externes
	{
		timer_extit = 1000;
		FLAG_IT = TRUE;
	}
	timer_extit--;
	if(!timer_lcd_display) //Compteur pour l'affichage de la distance
	{
		timer_lcd_display = 250;
		FLAG_DIST_DISPLAY = TRUE;
	}
	timer_lcd_display--;
	return 0;
}

/*
 * @brief Fonction qui d�tecte l'appui sur un bouton
 * @pre	  Le bouton doit �tre initialis� selon une broche en amont (ds le main par ex)
 * @author : Nirgal
 */
bool_e button_press_event(void)
{
	static bool_e previous_button = FALSE; //état précédent du bouton
	bool_e ret = FALSE;
	bool_e current_button; //état actuel du bouton
	//bouton en logique inverse, d'où le '!'
	current_button = !HAL_GPIO_ReadPin(YELLOW_BUTTON_GPIO, YELLOW_BUTTON_PIN);
	//si le bouton est appuyé et ne l'était pas avant, champomy !
	if(current_button && !previous_button)
	ret = TRUE;
	//on mémorise l'état actuel pour le prochain passage
	previous_button = current_button;
	return ret;
}

/*
 * @brief Cette fonction est executée à chaque appui sur une touche de la télécommande infrarouge
 * 		  Elle permet de changer de mode à distance.
 * @pre   Il faut préalablement autoriser les interruptions externes sur la broche correspondante
 * @pre   Elle "autorise" la modification du mode dans la machine à état générale (mode_management) via la variable ir_mode_update qui doit être vraie
 */
void US_REVERSING_RADAR_ir_change_mode(void)
{
	if(FLAG_IT)
	{
		if(current_mode == 4) //4 modes maxi
		{
			current_mode = 1; //On repasse au mode all
			ir_mode_update = TRUE;
		}

		else
		{
			current_mode += 1;//Mode suivant
			ir_mode_update = TRUE;
		}
		FLAG_IT = FALSE;
	}
}

/***************************MAIN PROCESS FUNCTIONS*****************************/

/*
 * @brief Fonction principale lan�ant le radar
 */
void US_REVERSING_RADAR_process_main(void)
{
	HCSR04_demo_state_machine();
	US_REVERSING_RADAR_mode_management();
	US_REVERSING_RADAR_compute_frequency(); //(Si on le met avant le mode management dist n'est pas encore divisée par 10)
}

/*
 * @brief Machine à état générale permettant de switcher entre les diff�rents modes du radar
 * @pre	  Cette fonction doit �tre appel�e en t�che de fond
 * @info  Elle est la seule à pouvoir modifier le mode en cours via une interrutpion (ir_mode_update) ou l'appui sur le bouton poussoir prévu à cet effet (GPIO_PIN_3)
 */
void US_REVERSING_RADAR_mode_management(void)
{
	static mode_e mode  = INIT;
	if(ir_mode_update)
	{
		mode = current_mode;
		ir_mode_update = FALSE;
		lcd_mode_update = TRUE;
		FLAG_IT = FALSE;
	}
	if(!t_read_button) //le chronom�tre est �puis�
	{
		t_read_button = 10; //on recharge le chronom�tre pour 10ms
		switch(mode)
		{
			case INIT:
				US_REVERSING_RADAR_init();
				mode = MODE_ALL;
				current_mode = mode;
				break;
			case MODE_ALL:
				distance = (uint16_t)(HCSR04_get_distance()/10); //on convertit en cm
				US_REVERSING_RADAR_lcd_display();
				US_REVERSING_RADAR_manage_peripherals();
				if(button_press_event())
				{
					lcd_mode_update = TRUE;
					mode = MODE_SCREEN_ONLY;
					current_mode = mode;
				}
				break;
			case MODE_SCREEN_ONLY:
				distance = (uint16_t)(HCSR04_get_distance()/10);
				US_REVERSING_RADAR_lcd_display();
				US_REVERSING_RADAR_manage_peripherals(); //Vrmt Utile ?
				if(button_press_event())
				{
					lcd_mode_update = TRUE;
					mode = MODE_BUZZER_ONLY;
					current_mode = mode;
				}
				break;
			case MODE_BUZZER_ONLY:
				distance = (uint16_t)(HCSR04_get_distance()/10);
				US_REVERSING_RADAR_lcd_display();
				US_REVERSING_RADAR_manage_peripherals();
				if(button_press_event())
				{
					lcd_mode_update = TRUE;
					mode = MODE_LED_ONLY;
					current_mode = mode;
				}
				break;
			case MODE_LED_ONLY:
				distance = (uint16_t)(HCSR04_get_distance()/10);
				US_REVERSING_RADAR_lcd_display();
				US_REVERSING_RADAR_manage_peripherals();
				if(button_press_event())
				{
					lcd_mode_update = TRUE;
					mode = MODE_ALL;
					current_mode = mode;
				}
				break;
			default :
				ILI9341_printf(50,100,&Font_11x18,ILI9341_COLOR_RED,ILI9341_COLOR_WHITE,"Mode error (state machine)");
				while(1){}
				break;
		}
	}
}

/*
 * @brief Fonction g�rant l'affichage sur l'�cran tft
 * @pre	  L'�cran doit �tre initialis� avant (via la fonction init dans le mode management)
 */
void US_REVERSING_RADAR_lcd_display(void)
{
	if(FLAG_DIST_DISPLAY)
	{
		//on affiche la valeur de la distance
		ILI9341_Rotate(ILI9341_Orientation_Landscape_2);
		ILI9341_DrawFilledRectangle(170,100,250,150,ILI9341_COLOR_WHITE); //Pour effacer l'affichage pr�c�dent et rendre �a propre (valeur distance)
		ILI9341_printf(176,100,&Font_11x18,ILI9341_COLOR_BLACK,ILI9341_COLOR_WHITE,"%d cm",distance);
		FLAG_DIST_DISPLAY = FALSE;
	}
	//on affiche le reste
	if(lcd_mode_update) // Pour �viter le clignotement lorsque l'on reste dans le m�me mode
	{
		ILI9341_Puts(75,10,"REVERSING RADAR", &Font_11x18, ILI9341_COLOR_BLUE, ILI9341_COLOR_WHITE);
		ILI9341_printf(50,100,&Font_11x18,ILI9341_COLOR_BLACK,ILI9341_COLOR_WHITE,"DISTANCE : ");
		ILI9341_DrawFilledRectangle(60,30,250,60,ILI9341_COLOR_WHITE);//On efface le précédent mode
		switch(current_mode)
		{
			case INIT :
				//N'arrive que si mode_management foire
				ILI9341_Puts(0,35,"Error in mode_management state machine", &Font_11x18, ILI9341_COLOR_RED, ILI9341_COLOR_WHITE);
				while(1){}
				break;
			case MODE_ALL :
				ILI9341_Puts(110,35,"Mode All", &Font_11x18, ILI9341_COLOR_CYAN, ILI9341_COLOR_WHITE);
				break;
			case MODE_SCREEN_ONLY :
				ILI9341_Puts(70,35,"Mode Screen Only", &Font_11x18, ILI9341_COLOR_ORANGE, ILI9341_COLOR_WHITE);
				break;
			case MODE_BUZZER_ONLY :
				ILI9341_Puts(70,35,"Mode Buzzer Only", &Font_11x18, ILI9341_COLOR_GREEN2, ILI9341_COLOR_WHITE);
				break;
			case MODE_LED_ONLY :
				ILI9341_Puts(80,35,"Mode Led Only", &Font_11x18, ILI9341_COLOR_RED, ILI9341_COLOR_WHITE);
				break;
			default :
				ILI9341_Puts(0,35,"Display mode error : case default", &Font_11x18, ILI9341_COLOR_RED, ILI9341_COLOR_WHITE);
				break;
		}
		lcd_mode_update = FALSE;
	}
}

/*
 * @brief Fonction d�terminant les p�r�ph�riques � activer ou non en fonction du mode
 * @pre	  Les p�r�ph�riques doivent �tre pr�alablement initialis�s (via fonction init dans le mode management)
 * @pre   Attention : cette fonction prend en compte la distance en cm (qui a �t� /10 depuis son calcul par la fonction demo de "HCSR04.c/h")
 * @info  frequency_timer est l'inverse de la freq (petite valeur --> grande freq/grande valeur --> petite freq) --> Agit comme un timer
 */
void US_REVERSING_RADAR_compute_frequency(void)
{
	if(distance > 55)
		frequency_timer = -1;
	else if(distance <=55 && distance > 40)
		frequency_timer = 600;
	else if(distance <40 && distance >30)
		frequency_timer = 500;
	else if(distance <30 && distance >20)
		frequency_timer = 400;
	else if(distance <20 && distance >12)
		frequency_timer = 250;
	else if(distance <12 && distance >8)
		frequency_timer = 150;
	else if(distance <8 && distance > 4)
		frequency_timer = 52;
	else if(distance < 4)
		frequency_timer = 0;
}

/*
 * @brief Fonction permettant d'allumer la led ou le buzzer en fonction de la fr�quence (donc de la distance).
 * 		  Cela fonctionne uniquement pour la led et le buzzer respectivements c�bl�s sur le GPIO10(D2) et GPIO5(D4)
 * @pre	  Le buzzer et la led doivent �tre initialis�s sur leurs broches respectives
 */
void US_REVERSING_RADAR_manage_peripherals(void)  //Revoir le timer avec le mode entrance
{
	switch(current_mode)
	{
		case INIT :
			//N'arrive normalement pas
			ILI9341_Puts(0,35,"Error managing peripherals state", &Font_11x18, ILI9341_COLOR_RED, ILI9341_COLOR_WHITE);
			while(1){}
			break;
		case MODE_ALL :
			if(!t)
			{
				if(!frequency_timer) //Entre 0 et 4 cm
				{
					HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,TRUE);
					HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,TRUE);
					break;
				}
				else if(frequency_timer == -1) //Distance trop importante (>55cm), on allume rien
				{
					HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,FALSE);
					HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,FALSE);
				}
				else //Entre 4 et 55cm
				{
				t = (uint32_t)frequency_timer;
				HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_10 ); //allumage de la led
				HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_5);	 //allumage du buzzer
				}
			}
			break;
		case MODE_SCREEN_ONLY :
			//Pas de led ni de buzzer
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,FALSE);
			HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,FALSE);
			break;
		case MODE_BUZZER_ONLY :
			if(!t)
			{
				if(!frequency_timer)
				{
					HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,TRUE); //buzzer on
					HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,FALSE); //led off
					break;
				}
				else if(frequency_timer == -1)
				{
					HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,FALSE);
					HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,FALSE);
				}
				else
				{
				t = (uint32_t)frequency_timer;
				HAL_GPIO_TogglePin(GPIOB,GPIO_PIN_5); //buzzer on
				HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,FALSE); //Led off
				}
			}
			break;
		case MODE_LED_ONLY :
			if(!t)
			{
				if(!frequency_timer)
				{
					HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,TRUE); //led on
					HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,FALSE); //buzzer off
					break;
				}
				else if(frequency_timer == -1)
				{
					HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,FALSE);
					HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,FALSE);
				}
				else
				{
				t = (uint32_t)frequency_timer;
				HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_10 ); //allumage de la led
				HAL_GPIO_WritePin(GPIOB,GPIO_PIN_5,FALSE); //buzzer off
				}
			}
			break;
		default :
			ILI9341_Puts(0,35,"Error managing peripherals state", &Font_11x18, ILI9341_COLOR_RED, ILI9341_COLOR_WHITE);
			break;
	}
}

/***************************END OF FILE*************************/
