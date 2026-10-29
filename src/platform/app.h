/* Vivre en app externe d'Epsilon sans mettre les fichiers en danger.
 *
 * Normalement, Epsilon quitte l'app dès qu'on appuie sur Maison, puis réutilise sa RAM pour ses
 * propres réserves de mémoire. Un indicateur rangé dans cette même RAM lui dit s'il doit les
 * reconstruire : une app qui l'a écrasé peut laisser le système sur une réserve abîmée, qui finit
 * par planter la calculatrice et effacer ses fichiers (sauvegardes comprises).
 *
 * app_begin() demande donc à Epsilon de retenir Maison pendant le jeu (le jeu la lit comme une
 * touche et quitte lui-même), et app_end() remet la RAM de l'app à zéro avant de la rendre.
 * Les appels système 10 et 13 sont ceux du « disjoncteur » d'Epsilon, relevés dans NumPlay ; à
 * vérifier sur la N0120. Sur PC, ces fonctions ne font rien. */
#ifndef APP_H
#define APP_H

void app_begin(void);
/* À appeler en dernier, sous la forme `return app_end();` : plus rien en RAM ne sert ensuite. */
int app_end(void);

#endif
