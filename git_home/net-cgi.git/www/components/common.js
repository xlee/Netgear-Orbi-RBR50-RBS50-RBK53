$(function(){$(".dropdown-menu li a").click(function(){var f=$(this).parent().parent();f.find("a.active").removeClass("active");$(this).addClass("active");var d=$(this).parent().parent().parent().find("button span").first();$(d).html($(this).text());var b=$(d).parent().prev();var c=$(this)[0].innerText;var e=$(this).parent().parent().parent().find("span").html(c);var a=$(this).parent();$(d).parent().prev().html(c)});$("ul li a").click(function(a){a.preventDefault()});$(".button-nav").click(function(f){var c=$(this);if(c.find(".ink").length==0){c.append("<span class='ink'></span>")}var b=c.find(".ink");b.removeClass("animate");if(!b.height()&&!b.width()){var g=Math.max(c.outerWidth(),c.outerHeight());b.css({height:g,width:g})}var a=f.pageX-c.offset().left-b.width()/2;var h=f.pageY-c.offset().top-b.height()/2;b.css({top:h+"px",left:a+"px"}).addClass("animate")});$(".input-wrapper").on("click",function(a){var c=$(this).find(".input-title")[0];var b=c.className.indexOf("ddl-title")>-1;if(b){return true}$(this).find(".input-title").addClass("active");$(this).find("input").focus();a.preventDefault()});$(".input-wrapper input").focus(function(b){var a=$(this).parent();a.find(".input-title").addClass("active");b.preventDefault()});$("input").on("blur",function(){$(this).parent().find(".input-title").removeClass("active").removeClass("non-active-val");if($(this).val()){$(this).parent().find(".input-title").addClass("non-active-val")}var a=this.className.indexOf("ip-box")>-1;if(a){var d=$(this).val();var e=255;if(d>e){$(this).val(e)}}var b=$(this);var c=/<script[^>]*>[\d\D]*?<\/script>/ig;b.val(b.val().replace(c,""))});setTimeout(function(){$("input:radio:first").prop("checked",true).trigger("click")},500)});